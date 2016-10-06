#include <tokenizer.h>
#include <ctype.h>
#include <string.h>
// TODO: Actually replace trigraphs with their respective symbols (textwise), remove comments (turn them into single space), turn tabs into spaces.
// TODO: Make errors non fatal, return malformed tokens.

// Newlines != whitespace
static bool is_whitespace(const char c) {
    return c == ' ' || c == '\t';
}

// TODO: Extend for utf-8?
static bool is_ident_start(const char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

// TODO: Extend for utf-8?
static bool is_ident_char(const char c) {
    return is_ident_start(c) || isdigit(c);
}

static bool is_hexadecimal_digit(const char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static bool is_octal_digit(const char c) {
    return c >= '0' && c <= '7';
}

static const char *const keywords[] = {
    "auto"    , "break"     , "case"     , "char"    , "const"    , "continue",
    "default" , "do"        , "double"   , "else"    , "enum"     , "extern"  ,
    "float"   , "for"       , "goto"     , "if"      , "inline"   , "int"     , 
    "register", "restrict"  , "return"   , "short"   , "signed"   , "long"    ,
    "sizeof"  , "static"    , "struct"   , "switch"  , "typedef"  ,
    "union"   , "unsigned"  , "void"     , "volatile", "while"    ,
    "_Alignas", "_Alignof"  , "_Atomic"  , "_Bool"   , "_Complex" ,
    "_Generic", "_Imaginary", "_Noreturn",
    "_Thread_local", "_Static_assert",
};

static bool is_keyword(const char *data, long int len) {
    for (size_t i = 0; i < sizeof(keywords) / sizeof(char *); ++i) {

        if (len != strlen(keywords[i])) continue;
        if (!strncmp(data, keywords[i], len)) return true;
    }
    return false;
}

// TODO: Lift numeric literals out of the tokenizer into the parser?
// TODO: Null terminators in numeric literal checks are missing.
// TODO: We need MUCH better errors.
void next_token(token *token, tokenizer_state *state) {
    // Pointer to our current character.
    const char *data;

    token->source.source_file = state->source_handle;

    #define PEEK_CHAR data[0]
    // Comma operator fun.
    #define INCR_CHAR (processed++, state->current_column += 1, c = *(data++))
    #define END_TOKEN(K) token->kind = K; goto end_token
    #define INCR_LINE { state->current_line += 1; state->current_column = 1; }
    #define OR_ASSIGN(K) { if(PEEK_CHAR == '=') { INCR_CHAR; END_TOKEN(K ## ASSIGN); } END_TOKEN(K); }

    char c;
    long int processed = 0;

recognize_token:
    data = handle_to_file(state->source_handle)->contents + state->current_index;
    token->source.offset = state->current_index;
    token->source.line = state->current_line;
    token->source.column = state->current_column;
    INCR_CHAR;

    if (c == '/') {
        // Check for comments:
        switch (PEEK_CHAR) {
            case '*':
                INCR_CHAR;
                token->kind = TOK_COMMENT;

                while (INCR_CHAR) {
                    if (c == '*' && PEEK_CHAR == '/') {
                        INCR_CHAR;
                        goto end_token;
                    }
                }
                // Hit '\0' before closing comment.
                sc_error(true, "Hit null terminator before end of multi line comment.");
            break;
            case '/':
                INCR_CHAR;
                token->kind = TOK_COMMENT;

                while (PEEK_CHAR != '\n' && PEEK_CHAR != '\r' && PEEK_CHAR != '\0') {
                    INCR_CHAR;
                }

                if (PEEK_CHAR == '\0') {
                    sc_error(true, "Hit null terminator before end of single line comment.");
                }

                goto end_token;
            break;
            default:
                // Just a slash
                // Check for compound assignment.
                OR_ASSIGN(TOK_SLASH);
            break;
        }
    }

    // Check for string literal.
    if (c == '"') {
        while (INCR_CHAR) {
            switch (c) {
                // Check for \" or \\\n
                case '\\':
                    if (PEEK_CHAR == '\\') {
                        INCR_CHAR;
                    } else if (PEEK_CHAR == '"') {
                        // Ok!
                        INCR_CHAR;
                    } else if (PEEK_CHAR == '\n') {
                        INCR_CHAR;
                        INCR_LINE;
                    } else if (PEEK_CHAR == '\r') {
                        INCR_CHAR;
                        if (PEEK_CHAR != '\n') {
                            sc_error(true, "Stray carriage return without newline.");
                        }
                        INCR_CHAR;
                        INCR_LINE;
                    }
                break;
                case '"':
                    END_TOKEN(TOK_STRINGLITERAL);
                break;
                case '\r':
                case '\n':
                    sc_error(true, "Newline in string literal...");
                break;
            }
        }

        assert (c == '\0');
        sc_error(true, "Hit null terminator before end of string literal.");
    }

    // Check for character literal.
    if (c == '\'') {
        while (INCR_CHAR) {
            switch (c) {
                // Check for \' or \\\n
                case '\\':
                    if (PEEK_CHAR == '\\') {
                        INCR_CHAR;
                    } else if (PEEK_CHAR == '\'') {
                        // Ok!
                        INCR_CHAR;
                    } else if (PEEK_CHAR == '\n') {
                        INCR_CHAR;
                        INCR_LINE;
                    } else if (PEEK_CHAR == '\r') {
                        INCR_CHAR;
                        if (PEEK_CHAR != '\n') {
                            sc_error(true, "Stray carriage return without newline.");
                        }
                        INCR_CHAR;
                        INCR_LINE;
                    }
                break;
                case '\'':
                    END_TOKEN(TOK_CHARACTERLITERAL);
                break;
                case '\r':
                case '\n':
                    sc_error(true, "Newline in character literal at line %d, cloumn %d.", state->current_line, state->current_column);
                break;
            }
        }

        assert (c == '\0');
        sc_error(true, "Hit null terminator before end of character literal.");
    }

    // Ignore "\\\n".
    if (c == '\\') {
        INCR_CHAR;

        // Carriage return perhaps?
        if (PEEK_CHAR == '\r') {
            INCR_CHAR;
            if (PEEK_CHAR != '\n') {
                sc_error(true, "Found carriage return without newline.");
            }
        }

        if (PEEK_CHAR == '\n') {
            INCR_CHAR;
            INCR_LINE;
            // Change the token start to eliminate that pesky, pesky slash-newline
            token->source.offset += processed;
            state->current_index += processed;
            processed = 0;
            goto recognize_token;
        } else {
            // TODO: I think this is correct.
            sc_error(true, "Found \\ not followed by newline in middle of input.");
            return;
        }
    }

    // Trigraphs
    if (c == '?') {
        if (PEEK_CHAR == '?') {
            INCR_CHAR;

            switch (INCR_CHAR) {
                case '=':
                    // We need to know if we are followed by a regular or a trigraph hash to emit doublehash instead of hash.
                    if (PEEK_CHAR == '#') {
                        INCR_CHAR;
                        END_TOKEN(TOK_DOUBLEHASH);
                    } else if (PEEK_CHAR == '?' && data[1] == '?' && data[2] == '=') {
                        // TODO: Bounds check that.
                        INCR_CHAR;
                        INCR_CHAR;
                        INCR_CHAR;
                        END_TOKEN(TOK_DOUBLEHASH);
                    }

                    END_TOKEN(TOK_HASH);
                break;
                case '(':
                    END_TOKEN(TOK_OPENSQBRACK);
                break;
                case ')':
                    END_TOKEN(TOK_CLOSESQBRACK);
                break;
                case '/':
                    if (PEEK_CHAR == '\r') {
                        INCR_CHAR;
                        if (PEEK_CHAR != '\n') {
                            sc_error(true, "Found carriage return without newline.");
                        }
                    }
                    if (PEEK_CHAR == '\n') {
                        INCR_CHAR;
                        INCR_LINE;
                        // Change the token start to eliminate that pesky, pesky slash-newline
                        token->source.offset += processed;
                        state->current_index += processed;
                        processed = 0;
                        goto recognize_token;
                    } else {
                        sc_error(true, "Found \\ (originating from ??" "/) not followed by whitespace in middle of input.");
                    }
                break;
                case '\'':
                    OR_ASSIGN(TOK_BITWISEXOR);
                break;
                case '<':
                    END_TOKEN(TOK_OPENBRACK);
                break;
                case '>':
                    END_TOKEN(TOK_CLOSEBRACK);
                break;
                case '!':
                    // We need to know if we are followed by a regular or a trigraph 'or' to emit logical or instead of bitwise or.
                    if (PEEK_CHAR == '|') {
                        INCR_CHAR;
                        END_TOKEN(TOK_LOGICALOR);
                    } else if (PEEK_CHAR == '?' && data[1] == '?' && data[2] == '!') {
                        // TODO: Bounds check that.
                        INCR_CHAR;
                        INCR_CHAR;
                        INCR_CHAR;
                        END_TOKEN(TOK_LOGICALOR);
                    }

                    OR_ASSIGN(TOK_BITWISEOR);
                break;
                case '-':
                    END_TOKEN(TOK_BITWISENOT);
                break;
                default:
                    sc_error(true, "Unrecognized trigraph.");
                break;
            }
        }

        END_TOKEN(TOK_QUESTIONMARK);
    }

    // Check for newline(s).
    if (c == '\n' || c == '\r') {
        if (c == '\n') { INCR_LINE; }
        while(PEEK_CHAR == '\n' || PEEK_CHAR == '\r') { INCR_CHAR; if (c == '\n') { INCR_LINE; } }
        END_TOKEN(TOK_NEWLINE);
    }

    // Check for whitespace.
    if (is_whitespace(c)) {
        while (is_whitespace(PEEK_CHAR)) { INCR_CHAR; }
        END_TOKEN(TOK_WHITESPACE);
    }

    // Check for keyword or identifier.
    if (is_ident_start(c)) {
        while (is_ident_char(PEEK_CHAR)) { INCR_CHAR; }
        
        if (is_keyword(data - processed, processed)) {
            END_TOKEN(TOK_KEYWORD);
        } else {
            END_TOKEN(TOK_IDENTIFIER);
        }
    }

    // Check for '#'
    if (c == '#') {
        if (PEEK_CHAR == '#') {
            INCR_CHAR;
            END_TOKEN(TOK_DOUBLEHASH);
        }

        END_TOKEN(TOK_HASH);
    }

    if (c == '[') {
        END_TOKEN(TOK_OPENSQBRACK);
    }

    if (c == ']') {
        END_TOKEN(TOK_CLOSESQBRACK);
    }

    if (c == '^') {
        END_TOKEN(TOK_BITWISEXOR);
    }

    if (c == '{') {
        END_TOKEN(TOK_OPENBRACK);
    }

    if (c == '}') {
        END_TOKEN(TOK_CLOSEBRACK);
    }

    if (c == '|') {
        if (PEEK_CHAR == '|') {
            INCR_CHAR;
            END_TOKEN(TOK_LOGICALOR);
        }

        OR_ASSIGN(TOK_BITWISEOR);
    }

    if (c == '&') {
        if (PEEK_CHAR == '&') {
            INCR_CHAR;
            END_TOKEN(TOK_LOGICALAND);
        }

        OR_ASSIGN(TOK_BITWISEAND);
    }

    if (c == '~') {
        END_TOKEN(TOK_BITWISENOT);
    }

    if (c == '(') {
        END_TOKEN(TOK_OPENPAREN);
    }

    if (c == ')') {
        END_TOKEN(TOK_CLOSEPAREN);
    }

    if (c == '*') {
        OR_ASSIGN(TOK_STAR);
    }

    if (c == '+') {
        if (PEEK_CHAR == '+') {
            INCR_CHAR;
            END_TOKEN(TOK_INCREMENT);
        }

        OR_ASSIGN(TOK_PLUS);
    }

    if (c == '-') {
        if (PEEK_CHAR == '-') {
            INCR_CHAR;
            END_TOKEN(TOK_DECREMENT);
        } else if (PEEK_CHAR == '>') {
            // -> operator
            INCR_CHAR;
            END_TOKEN(TOK_ARROW);
        }

        OR_ASSIGN(TOK_MINUS);
    }

    if (c == '%') {
        OR_ASSIGN(TOK_MODULO);
    }

    if (c == '<') {
        if (PEEK_CHAR == '<') {
            INCR_CHAR;
            OR_ASSIGN(TOK_LEFTSHIFT);
        } else if (PEEK_CHAR == '=') {
            INCR_CHAR;
            END_TOKEN(TOK_LESSTHANEQ);
        }

        END_TOKEN(TOK_LESSTHAN);
    }

    if (c == '>') {
        if (PEEK_CHAR == '>') {
            INCR_CHAR;
            OR_ASSIGN(TOK_RIGHTSHIFT);
        } else if (PEEK_CHAR == '=') {
            INCR_CHAR;
            END_TOKEN(TOK_GREATERTHANEQ);
        }

        END_TOKEN(TOK_GREATERTHAN);
    }

    if (c == '!') {
        if (PEEK_CHAR == '=') {
            INCR_CHAR;
            END_TOKEN(TOK_NEQUALS);
        }

        END_TOKEN(TOK_BANG);
    }

    if (c == '=') {
        if (PEEK_CHAR == '=') {
            INCR_CHAR;
            END_TOKEN(TOK_EQUALS);
        }

        END_TOKEN(TOK_ASSIGN);
    }

    if (c == ':') {
        END_TOKEN(TOK_COLON);
    }

    if (c == ';') {
        END_TOKEN(TOK_SEMICOLON);
    }

    if (c == ',') {
        END_TOKEN(TOK_COMMA);
    }

    // Check for numeric literals
    // If we start with a dot and then a digit, we are a float literal for sure.
    if (c == '.' && isdigit(PEEK_CHAR)) {
        INCR_CHAR;
        while (isdigit(PEEK_CHAR)) { INCR_CHAR; }
        token->kind = TOK_DECIMALFLOATLITERAL;
        goto exponent_optional;

    } else if (c == '.') {
        // Any other dot instance is the dot operator.
        END_TOKEN(TOK_DOT);
    }

    // Some kind of numeric literal.
    if (isdigit(c)) {

        // single digit into exponent or dot, we are a floating point number.
        switch (PEEK_CHAR) {
            case 'e':
            case 'E':
                token->kind = TOK_DECIMALFLOATLITERAL;
                goto exponent_optional;
            break;
            case '.':
                INCR_CHAR;
                while (isdigit(PEEK_CHAR)) { INCR_CHAR; }
                token->kind = TOK_DECIMALFLOATLITERAL;
                goto exponent_optional;
            break;
        }

        if (c == '0') {
            // Could be a single zero, an octal integer literal, a hexadecimal (integer or float) literal or a decimal float literal.
            // We know that if the next char was e, E or . we wouldn't be here.
            if (PEEK_CHAR == 'x' || PEEK_CHAR == 'X') {
                // Hexadecimal literal.
                INCR_CHAR;

                // If we are followed by a dot, we are a hexadecimal float literal for sure.
                if (PEEK_CHAR == '.') {
                    token->kind = TOK_HEXADECIMALFLOATLITERAL;
                    INCR_CHAR;
                    if (!is_hexadecimal_digit(PEEK_CHAR)) {
                        sc_error(true, "Expected hexadecimal digits after 0x. floating point literal.");
                    }
                    INCR_CHAR;
                    while (is_hexadecimal_digit(PEEK_CHAR)) { INCR_CHAR; }
                    goto binary_exponent;
                }

                if (!is_hexadecimal_digit(PEEK_CHAR)) {
                    sc_error(true, "Expected hexadecimal digits after 0x hexadecimal literal notation.");
                }
                INCR_CHAR;
                while (is_hexadecimal_digit(PEEK_CHAR)) { INCR_CHAR; }
            
                // Now, we can be a floating point literal if we have a dot or binary exponent notation.
                switch (PEEK_CHAR) {
                    case '.':
                        // Here, the rest of the digits are optional.
                        INCR_CHAR;
                        while (is_hexadecimal_digit(PEEK_CHAR)) { INCR_CHAR; }
                        token->kind = TOK_HEXADECIMALFLOATLITERAL;
                        goto binary_exponent;
                    break;
                    case 'p':
                    case 'P':
                        token->kind = TOK_HEXADECIMALFLOATLITERAL;
                        goto binary_exponent;
                    break;
                    default:
                        // Finally, a hexadecimal integer literal
                        token->kind = TOK_HEXADECIMALINTEGERLITERAL;
                        goto integer_suffix;
                    break;
                }
            } else if (is_octal_digit(PEEK_CHAR)) {
                // We have an octal digit next.
                // Note that this could still be a decimal floating point literal.
                // Let's skip through as much octal as we can.
                INCR_CHAR;
                while (is_octal_digit(PEEK_CHAR)) { INCR_CHAR; }
                if (isdigit(PEEK_CHAR)) {
                    // Ok, we have a decimal digit in the sequence.
                    // This can only mean we are a floating point decimal.
                    // Let's skip through the decimals.
                    INCR_CHAR;
                    while (isdigit(PEEK_CHAR)) { INCR_CHAR; }
                    // We NEED to have a dot here.
                    if (PEEK_CHAR != '.') {
                        sc_error(true, "Expected . while tokenizing a series of decimal numbers starting with zero.");
                    }

                    INCR_CHAR;
                    // We may or may not have decimal digits after the dot.
                    while (isdigit(PEEK_CHAR)) { INCR_CHAR; }
                    token->kind = TOK_DECIMALFLOATLITERAL;
                    goto exponent_optional;
                } else if (PEEK_CHAR == '.') {
                    // Ok, our floating point number happened to only have octal digits in it. No big deal.
                    INCR_CHAR;
                    while (isdigit(PEEK_CHAR)) { INCR_CHAR; }
                    token->kind = TOK_DECIMALFLOATLITERAL;
                    goto exponent_optional;
                } else {
                    // Anything else, this is an octal integer point literal.
                    token->kind = TOK_OCTALINTEGERLITERAL;
                    goto integer_suffix;
                }
            } else if (isdigit(PEEK_CHAR)) {
                // Non octal decimal digit.
                // We must be a decimal floating point literal.
                INCR_CHAR;
                while (isdigit(PEEK_CHAR)) { INCR_CHAR; }
                // We NEED a .
                if (PEEK_CHAR != '.') {
                    sc_error(true, "Expected . while tokenizing a series of decimal numbers starting with zero.");
                }
                INCR_CHAR;
                while (isdigit(PEEK_CHAR)) { INCR_CHAR; }
                token->kind = TOK_DECIMALFLOATLITERAL;
                goto exponent_optional;
            } else if (PEEK_CHAR == '.') {
                // 0.(...)
                INCR_CHAR;
                token->kind = TOK_DECIMALFLOATLITERAL;
                while (isdigit(PEEK_CHAR)) { INCR_CHAR; }
                goto exponent_optional;
            } else {
                // Just a single zero dude, chill out.
                token->kind = TOK_DECIMALINTEGERLITERAL;
                goto integer_suffix;
            }
        } else {
            // Consume all digits.
            while (isdigit(PEEK_CHAR)) { INCR_CHAR; }
            // If we have a . or e or E we are a decimal float literal.
            // If we don't we are a decimal int literal.
            switch (PEEK_CHAR) {
                case '.':
                    INCR_CHAR;
                    while (isdigit(PEEK_CHAR)) { INCR_CHAR; }
                    token->kind = TOK_DECIMALFLOATLITERAL;
                    goto exponent_optional;
                break;
                case 'e':
                case 'E':
                    token->kind = TOK_DECIMALFLOATLITERAL;
                    goto exponent_optional;
                break;
                default:
                    token->kind = TOK_DECIMALINTEGERLITERAL;
                    goto integer_suffix;
                break;
            }
        }
    }

    // Unrecognized character.
    if (c != '\0') {
        sc_error(true, "Unrecognized character with character code %d in middle of input.", c);
    }

    // Found '\0'
    if (state->current_index + processed != handle_to_file(state->source_handle)->size + 1) {
        // Todo: add file name + line + column
        sc_error(true, "Found null terminator in middle of input (%d out of %d processed)", state->current_index + processed
                                                                                , handle_to_file(state->source_handle)->size + 1);
    } else {
        // Ok, we are at end of input.
        token->kind = TOK_EOF;
        goto end_token;
    }

integer_suffix:
    switch (PEEK_CHAR) {
        case 'u':
        case 'U':
            INCR_CHAR;
            switch (PEEK_CHAR) {
                case 'l':
                    INCR_CHAR;
                    if (PEEK_CHAR == 'l') INCR_CHAR;
                break;
                case 'L':
                    INCR_CHAR;
                    if (PEEK_CHAR == 'L') INCR_CHAR;
                break;
            }
        break;
        case 'l':
            INCR_CHAR;
            switch (PEEK_CHAR) {
                case 'l':
                    INCR_CHAR;
                    if (PEEK_CHAR == 'u' || PEEK_CHAR == 'U') INCR_CHAR;
                break;
                case 'u':
                case 'U':
                    INCR_CHAR;
                break;
            }
        break;
        case 'L':
            INCR_CHAR;
            switch (PEEK_CHAR) {
                case 'L':
                    INCR_CHAR;
                    if (PEEK_CHAR == 'u' || PEEK_CHAR == 'U') INCR_CHAR;
                break;
                case 'u':
                case 'U':
                    INCR_CHAR;
                break;
            }
        break;
    }
    goto end_token;

binary_exponent:
    switch (PEEK_CHAR) {
        case 'p':
        case 'P':
            INCR_CHAR;
            if (PEEK_CHAR == '+' || PEEK_CHAR == '-') INCR_CHAR;
            if (!isdigit(PEEK_CHAR)) {
                sc_error(true, "Expected digit after binary exponent notation while tokenizing hexadecimal floating point literal.");
            }
            INCR_CHAR;
            while (isdigit(PEEK_CHAR)) { INCR_CHAR; }
        break;
        default:
            sc_error(true, "Expected binary exponent notation wile tokenizing hexadecimal floating point literal.");
        break;
    }
    goto float_suffix;
exponent_optional:
    switch (PEEK_CHAR) {
        case 'e':
        case 'E':
            INCR_CHAR;
            if (PEEK_CHAR == '+' || PEEK_CHAR == '-') INCR_CHAR;
            if (!isdigit(PEEK_CHAR)) {
                sc_error(true, "Expected digit after exponent notation while tokenizing decimal floating point literal.");
            }
            INCR_CHAR;
            while (isdigit(PEEK_CHAR)) { INCR_CHAR; }
        break;
    }
float_suffix:
    switch (PEEK_CHAR) {
        case 'f':
        case 'l':
        case 'F':
        case 'L':
            INCR_CHAR;
        break;
    }

    #undef OR_ASSIGN
    #undef PEEK_CHAR
    #undef INCR_CHAR
    #undef END_TOKEN
    #undef INCR_LINE
end_token:
    token->source.size = processed;
    state->current_index += processed;
}

void tokenizer_state_init(tokenizer_state *state, sc_file_cache_handle handle) {
    state->source_handle = handle;
    state->current_index = 0;
    state->current_line = 1;
    state->current_column = 1;
}

char *token_data(token *tok) {
    return handle_to_file(tok->source.source_file)->contents + tok->source.offset;
}

long int token_size(token *tok) {
    return tok->source.size;
}

char *zero_term_from_token(token *current) {
    long int tok_size = token_size(current);
    char *buffer = malloc(tok_size + 1);
    strncpy(buffer, token_data(current), tok_size);
    buffer[tok_size] = '\0';
    return buffer;
}

void skip_to(token *current, tokenizer_state *tok_state, token_kind kind) {
    do {
        next_token(current, tok_state);
    } while(current->kind != kind && current->kind != TOK_EOF);
}

char *unescape(const char *start, long int len) {
    // Do the actual unescaping you lazy sod.
    char *str = malloc(len + 1);
    strncpy(str, start, len);
    str[len] = '\0';
    return str;
}

bool tok_str_cmp(token *current, const char *str) {
    size_t len = strlen(str);
    if (token_size(current) != len) return false;

    char *tok_data = token_data(current);
    return !strncmp(tok_data, str, len);
}

// Skips whitespace + comments.
// Returns true if we skipped any whitespace.
bool skip_whitespace(token *current, tokenizer_state *state) {
    next_token(current, state);
    if (current->kind != TOK_WHITESPACE && current->kind != TOK_COMMENT) {
        return false;
    }

    do {
        next_token(current, state);
    } while (current->kind == TOK_WHITESPACE || current->kind == TOK_COMMENT);

    return true;
}
