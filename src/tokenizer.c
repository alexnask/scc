#include <tokenizer.h>
#include <ctype.h>
#include <string.h>
// TODO: Actually replace trigraphs with their respective symbols (textwise), remove comments (turn them into single space), turn tabs into spaces.
// TODO: Make errors non fatal, return malformed tokens.

// Newlines != whitespace
static bool is_whitespace(const char c) {
    return c == ' ' || c == '\t' || c == '\v' || c == '\f';
}

// TODO: Extend for utf-8?
static bool is_ident_start(const char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

// TODO: Extend for utf-8?
static bool is_ident_char(const char c) {
    return is_ident_start(c) || isdigit(c);
}

// Initial processing
static void initial_processing(tokenizer_state *state) {
    // So, we go through our text and process things like trigraphs, backslash + newline etc.
    // We fill the state's string with the final characters the tokenizer is meant to process.
    // This also gives us a nice error recovery strategy for the tokenizer (just leave the rest of the current string and ask for a new one).
    // assert(state->done == string_size(&data->current_data));

    string *current = &data->current_data;
    // Reset our string.
    string_resize(current, 0);
    state->done = 0;
    state->whitespace = false;
    state->line_start = state->line_end;
    state->column_start = state->column_end;

    char * const data = state->data + index;

    #define BOUNDS_CHECK if (state->index == state->data_size - 1)
    #define PEEK_CHAR data[0]
    #define ADD_CHAR { string_push(current, PEEK_CHAR); data++; state->index++; state->column_end++; }
    #define SKIP_CHAR { data++; state->index++; state->column_end++; }
    #define SKIP_CHAR_OR_ADD { state->index++; BOUNDS_CHECK { string_push(current, PEEK_CHAR); return; } data++; state->column_end++; }
    #define ADD_LINE { state->line_end++; state->column_end = 1; }

    // We keep going until we find a newline, eof or whitespace (which we all skip and don't insert into our string).
    while (true) {
        // We found EOF.
        BOUNDS_CHECK {
            break;
        }

        if (PEEK_CHAR == '\n') {
            size_t initial_str_size = string_size(current);
            ADD_CHAR;
            ADD_LINE;

            // Ok, let's find out if we are supposed to bail.
            // If we have a backslash in our input right before the newline, we have to go on.
            if (initial_str_size != 0) {
                // We could have had a backslash right before, check it.
                if (string_data(current)[initial_str_size - 1] == '\\') continue;
                else if (initial_str_size > 1 && (string_data(current)[initial_str_size - 1] == '\r') &&
                         string_data(current)[initial_str_size - 2] == '\\') continue;
                else break;
            }
        } else if (PEEK_CHAR == '?') {
            SKIP_CHAR_OR_ADD;

            if (PEEK_CHAR == '?') {
                SKIP_CHAR_OR_ADD;

                // May be a trigraph.
                switch (PEEK_CHAR) {
                    case '(':
                        SKIP_CHAR;
                        string_push(current, '[');
                    break;
                    case ')':
                        SKIP_CHAR;
                        string_push(current, ']');
                    break;
                    case '<':
                        SKIP_CHAR;
                        string_push(current, '{');
                    break;
                    case '>':
                        SKIP_CHAR;
                        string_push(current, '}');
                    break;
                    case '=':
                        SKIP_CHAR;
                        string_push(current, '#');
                    break;
                    case '/':
                        SKIP_CHAR;
                        string_push(current, '\\');
                    break;
                    case '\'':
                        SKIP_CHAR;
                        string_push(current, '^');
                    break;
                    case '!':
                        SKIP_CHAR;
                        string_push(current, '|');
                    break;
                    case '-':
                        SKIP_CHAR;
                        string_push(current, '~');
                    break;
                    default:
                        // Not a trigraph after all, write all.
                        string_append_ptr_size(current, data - 2, 3);
                        data++;
                        state->index++;
                        state->column++;
                    break;
                }
            }
        } else if (is_whitespace(PEEK_CHAR)) {
            SKIP_CHAR;
            do {
                BOUNDS_CHECK { break; }
                if (is_whitespace(PEEK_CHAR)) SKIP_CHAR;
                else break;
            } while(true);
            state->whitespace = true;
            return;
        } else ADD_CHAR;
    }

    #undef ADD_LINE
    #undef SKIP_CHAR_OR_ADD
    #undef SKIP_CHAR
    #undef ADD_CHAR
    #undef PEEK_CHAR
    #undef BOUNDS_CHECK
}

void next_token(pp_token *token, tokenizer_state *state) {
    string *current = &state->current_data;
    size_t chunk_size = string_size(current);

    if (state->done == chunk_size) {
        // Pull some processed data to tokenize.
        // Note that chunks end with whitespace or newlines, so we may need to pull more data in many cases.
        initial_processing(state);
        chunk_size = string_size(current);
    }

    char * const data = string_data(current) + state->done;
    size_t processed = 0;

    // TODO: What happens if we pull an exmpty chunk? Should check and repull.
token_start:
    // Token prologue
    token->source.path = state->path;
    token->source.line = state->line_start;
    token->source.column = state->column_start;

    #define PEEK_CHAR data[0]
    #define ADD_CHAR { data++; processed++; }
    #define HAS_CHAR (processed - state->done < chunk_size)
    #define PULL_CHUNK { initial_processing(state); chunk_size = string_size(current); processed = 0; data = string_data(current); }

    // Let's start with newlines
    if (PEEK_CHAR == '\r') {
        ADD_CHAR;
        if (!HAS_CHAR || PEEK_CHAR != '\n') {
            sc_error(false, "Expected \\n after \\r...");
            // Recover error.
            PULL_CHUNK;
            goto token_start;
        } else {
            ADD_CHAR;
            token->kind = PP_TOK_NEWLINE;
        }
    } else if (is_ident_start(PEEK_CHAR)) {
        // Identifiers
        ADD_CHAR;

        while (HAS_CHAR && is_ident_char(PEEK_CHAR)) {
            ADD_CHAR;
        }

        token->kind = PP_TOK_IDENTIFIER;
    }
    // Punctuators
    else if (PEEK_CHAR == '#') {
        ADD_CHAR;
        if (HAS_CHAR && PEEK_CHAR == '#') {
            ADD_CHAR;
            token->kind = PP_TOK_DOUBLEHASH;
        } else {
            token->kind = PP_TOK_HASH;
        }
    } else if (PEEK_CHAR == '.') {
        ADD_CHAR;
        if (HAS_CHAR && isdigit(PEEK_CHAR)) {
            goto do_number;
        } else {
            token->kind = PP_TOK_DOT;
        }
    } else if (PEEK_CHAR == '-') {
        ADD_CHAR;
        if (HAS_CHAR && PEEK_CHAR == '>') {
            ADD_CHAR;
            token->kind = PP_TOK_ARROW;
        } else if (HAS_CHAR && PEEK_CHAR == '-') {
            ADD_CHAR;
            token->kind = PP_TOK_DECREMENT;
        } else if (HAS_CHAR && PEEK_CHAR == '=') {
            ADD_CHAR;
            token->kind = PP_TOK_MINUS_ASSIGN;
        } else {
            token->kind = PP_TOK_MINUS;
        }
    } else if (PEEK_CHAR == ',') {
        ADD_CHAR;
        token->kind = PP_TOK_COMMA;
    } else if (PEEK_CHAR == '?') {
        ADD_CHAR;
        token->kind = PP_TOK_QUESTION_MARK;
    } else if (PEEK_CHAR == '=') {
        ADD_CHAR;
        if (HAS_CHAR && PEEK_CHAR == '=') {
            ADD_CHAR;
            token->kind = PP_TOK_EQUALS;
        } else {
            token->kind = PP_TOK_ASSIGN;
        }
    } else if (PEEK_CHAR == '+') {
        ADD_CHAR;
        if (HAS_CHAR && PEEK_CHAR == '+') {
            ADD_CHAR;
            token->kind = PP_TOK_INCREMENT;
        } else if (HAS_CHAR && PEEK_CHAR == '=') {
            ADD_CHAR;
            token->kind = PP_TOK_PLUS_ASSIGN;
        } else {
            token->kind = PP_TOK_PLUS;
        }
    } else if (PEEK_CHAR == '*') {
        ADD_CHAR;
        if (HAS_CHAR && PEEK_CHAR == '=') {
            ADD_CHAR;
            token->kind = PP_TOK_STAR_ASSIGN;
        } else {
            token->kind = PP_TOK_STAR;
        }
    } else if (PEEK_CHAR == '/') {
        ADD_CHAR;
        if (HAS_CHAR && PEEK_CHAR == '=') {
            ADD_CHAR;
            token->kind = PP_TOK_DIV_ASSIGN;
        } else {
            token->kind = PP_TOK_DIV;
        }
    } else if (PEEK_CHAR == '%') {
        ADD_CHAR;
        if (HAS_CHAR && PEEK_CHAR == '=') {
            ADD_CHAR;
            token->kind = PP_TOK_MOD_ASSIGN;
        } else if (HAS_CHAR && PEEK_CHAR == '>') {
            ADD_CHAR;
            token->kind = PP_TOK_CLOSE_BRACKET;
        } else if (HAS_CHAR && PEEK_CHAR == ':') {
            ADD_CHAR;
            // Check for double hash here.
            if (HAS_CHAR && PEEK_CHAR == '#') {
                ADD_CHAR;
                token->kind = PP_TOK_DOUBLEHASH;
            } else if (HAS_CHAR && PEEK_CHAR == '%' && processed - state_done < chunk_size - 1 && data[1] == ':') {
                ADD_CHAR; ADD_CHAR;
                token->kind = PP_TOK_DOUBLEHASH;
            } else {
                token->kind = PP_TOK_HASH;
            }
        } else {
            token->kind = PP_TOK_MOD;
        }
    } else if (PEEK_CHAR == '!') {
        ADD_CHAR;
        if (HAS_CHAR && PEEK_CHAR == '=') {
            ADD_CHAR;
            token->kind = PP_TOK_NOT_EQUALS;
        } else {
            token->kind = PP_TOK_LOGICAL_NOT;
        }
    } else if (PEEK_CHAR == '>') {
        ADD_CHAR;
        if (HAS_CHAR && PEEK_CHAR == '=') {
            ADD_CHAR;
            token->kind = PP_TOK_GREATER_EQUALS;
        } else if (HAS_CHAR && PEEK_CHAR == '>') {
            ADD_CHAR;
            if (HAS_CHAR && PEEK_CHAR == '=') {
                ADD_CHAR;
                token->kind = PP_TOK_RIGHT_SHIFT_ASSIGN;
            } else {
                token->kind = PP_TOK_RIGHT_SHIFT;
            }
        } else {
            token->kind = PP_TOK_GREATER;
        }
    } else if (PEEK_CHAR == '<') {
        ADD_CHAR;
        if (HAS_CHAR && PEEK_CHAR == '=') {
            ADD_CHAR;
            token->kind = PP_TOK_LESS_EQUALS;
        } else if (HAS_CHAR && PEEK_CHAR == '<') {
            ADD_CHAR;
            if (HAS_CHAR && PEEK_CHAR == '=') {
                ADD_CHAR;
                token->kind = PP_TOK_LEFT_SHIFT_ASSIGN;
            } else {
                token->kind = PP_TOK_LEFT_SHIFT;
            }
        } else {
            token->kind = PP_TOK_LESS;
        }
    } else if (PEEK_CHAR == '&') {
        ADD_CHAR;
        if (HAS_CHAR && PEEK_CHAR == '=') {
            ADD_CHAR;
            token->kind = PP_TOK_BITWISE_AND_ASSIGN;
        } else if (HAS_CHAR && PEEK_CHAR == '&') {
            ADD_CHAR;
            token->kind = PP_TOK_LOGICAL_AND;
        } else {
            token->kind = PP_TOK_BITWISE_AND;
        }
    } else if (PEEK_CHAR == '|') {
        ADD_CHAR;
        if (HAS_CHAR && PEEK_CHAR == '=') {
            ADD_CHAR;
            token->kind = PP_TOK_BITWISE_OR_ASSIGN;
        } else if (HAS_CHAR && PEEK_CHAR == '|') {
            ADD_CHAR;
            token->kind = PP_TOK_LOGICAL_OR;
        } else {
            token->kind = PP_TOK_BITWISE_OR;
        }
    } else if (PEEK_CHAR == '~') {
        ADD_CHAR;
        token->kind = PP_TOK_BITWISE_NOT;
    } else if (PEEK_CHAR == '^') {
        ADD_CHAR;
        if (HAS_CHAR && PEEK_CHAR == '=') {
            ADD_CHAR;
            token->kind = PP_TOK_BITWISE_XOR_ASSIGN;
        } else {
            token->kind = PP_TOK_BITWISE_XOR;
        }
    } else if (PEEK_CHAR == '[') {
        ADD_CHAR;
        token->kind = PP_TOK_OPEN_SQUARE_BRACKET;
    } else if (PEEK_CHAR == ']') {
        ADD_CHAR;
        token->kind = PP_TOK_CLOSE_SQUARE_BRACKET;
    } else if (PEEK_CHAR == '{') {
        ADD_CHAR;
        token->kind = PP_TOK_OPEN_BRACKET;
    } else if (PEEK_CHAR == '}') {
        ADD_CHAR;
        token->kind = PP_TOK_CLOSE_BRACKET;
    } else if (PEEK_CHAR == '(') {
        ADD_CHAR;
        token->kind = PP_TOK_OPEN_PAREN;
    } else if (PEEK_CHAR == ')') {
        ADD_CHAR;
        token->kind = PP_TOK_CLOSE_PAREN;
    } else if (PEEK_CHAR == ';') {
        ADD_CHAR;
        token->kind = PP_TOK_SEMICOLON;
    } else if (isdigit(PEEK_CHAR)) {
    do_number:
        ADD_CHAR;
        token->kind = PP_TOK_NUMBER;
        // TODO: Do rest here.
        // Then include headers (separate function called from the preprocessor)
        // Then string literals (keep whitespace, give chunks until we either hit newline or closing quote)
        // Then character literals (same as above)
        // Then EOF.
        // Then the rest
        // Then "regular" token type
        // With token source that can be "file" or "define" (which can point to other sources.)
        // (Or a source stack for single allocation, less fragmentation, iteration to produce error message)
    }

    #undef PULL_CHUNK
    #undef HAS_CHAR
    #undef ADD_CHAR
    #undef PEEK_CHAR

    state->done += processed;
    // Token epilogue
    assert(processed != 0);
    if (state->done == chunk_size) token->whitespace = state->whitespace;
    // Copy over the part that concerns the token.
    substring(&token->data, current, state->done, state->done + processed);
}

typedef struct tokenizer_state {
    // File path
    char * const path;

    // Current column and line in input.
    size_t line;
    size_t column;

    // Pointer to our constant file data.
    char * const data;
    // Current index + whole size.
    size_t index;
    size_t data_size;

    // Data after initial processing that we are tokenizing.
    string current_data;
    // How many bytes out of the current_data have been processed.
    size_t done;
} tokenizer_state;

void tokenizer_state_init(tokenizer_state *state, sc_file_cache_handle handle) {
    state->path = handle_to_file(handle)->abs_path;
    state->line_start = state->line_end = 1;
    state->column_start = state->column_end = 1;
    state->data = handle_to_file(handle)->contents;
    state->index = 0;
    state->data_size = handle_to_file(handle)->size;
    string_init(&state->current_data, 0);
    state->done = 0;
}
