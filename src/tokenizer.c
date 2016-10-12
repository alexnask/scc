#include <tokenizer.h>
#include <token_vector.h>
#include <ctype.h>
#include <string.h>

// Newlines != whitespace
static bool is_whitespace(const char c) {
    return c == ' ' || c == '\t' || c == '\v' || c == '\f';
}

static bool is_ident_start(const char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool is_ident_char(const char c) {
    return is_ident_start(c) || isdigit(c);
}

// Returns false when we hit EOF.
static bool get_processed_line(tokenizer_state *state) {
    string *current = &state->current_data;
    string_resize(current, 0);

    state->done = 0;

    state->line_start = state->line_end;
    state->column_start = state->column_end;

    const char *data = state->data + state->index;

    #define HAS_CHARS(N) (state->index + N < state->data_size)

    while (*data != '\n') {
        // Find backslash newline
        if (HAS_CHARS(1) && *data == '\\' && data[1] == '\n') {
            // Skip over.
            data += 2;
            state->index += 2;
            state->line_end++;
            state->column_end = 1;
        }
        // Trigraphs
        else if (HAS_CHARS(3) && *data == '?' && data[1] == '?') {
            bool trigraph = true;
            switch (data[2]) {
                case '(':
                    string_push(current, '[');
                break;
                case ')':
                    string_push(current, ']');
                break;
                case '<':
                    string_push(current, '{');
                break;
                case '>':
                    string_push(current, '}');
                break;
                case '=':
                    string_push(current, '#');
                break;
                case '/':
                    string_push(current, '\\');
                break;
                case '\'':
                    string_push(current, '^');
                break;
                case '!':
                    string_push(current, '|');
                break;
                case '-':
                    string_push(current, '~');
                break;
                default:
                    trigraph = false;
                break;
            }

            if (!trigraph) {
                string_append_ptr_size(current, data, 3);
                state->column_end += 3;
            } else {
                state->column_end++;
            }
            data += 3;
            state->index += 3;
        } else if (HAS_CHARS(1) && *data == '\r' && data[1] == '\n') {
            data++;
            state->index++;
        } else {
            string_push(current, *data);
            data++;
            state->index++;
            state->column_end++;
        }

        // We check against -1 so as not to include the terminating null.
        if (state->index == state->data_size - 1) return false;
    }

    // Skip past the newline character.
    data++;
    state->index++;

    state->line_end++;
    state->column_end = 1;

    #undef HAS_CHARS

    return true;
}

static void push_token(pp_token_vector *vec, tokenizer_state *state, size_t *processed, pp_token_kind kind) {
    static pp_token_kind last_token_kind = PP_TOK_WHITESPACE;

    pp_token tok;
    substring(&tok.data, &state->current_data, state->done, state->done + *processed);
    tok.kind = kind;

    if (last_token_kind == PP_TOK_HASH && kind == PP_TOK_IDENTIFIER) {
        if (string_equals_ptr_size(&tok.data, "include", sizeof("include") - 1)) {
            state->in_include = true;
        }
    }

    tok.source.path = state->path;
    tok.source.line = state->line_start;
    tok.source.column = state->column_start;

    pp_token_vector_push(vec, &tok);

    state->column_start += *processed;

    state->done += *processed;
    *processed = 0;

    if (kind != PP_TOK_WHITESPACE) {
        last_token_kind = kind;
    }
}

bool tokenize_line(pp_token_vector *vec, tokenizer_state *state) {
    // Get a processed line.
    bool result = get_processed_line(state);

    size_t line_size = string_size(&state->current_data);
    const char *data = string_data(&state->current_data);
    size_t processed = 0;

    // So, our line is ine "state->current_data"
    if (state->in_multiline_comment) {
        // We still are in some multiline comment, skip until we find the end (if we do)
        while (processed + state->done < line_size - 1) {
            if (data[processed + state->done] == '*' && data[processed + state->done + 1] == '/') {
                state->in_multiline_comment = false;
                processed += 2;
                push_token(vec, state, &processed, PP_TOK_WHITESPACE);
            }

            state->column_start++;
            processed++;
        }

        // We couldn't get out yet.
        // However, no line left :O
        if (state->in_multiline_comment && !result) {
            sc_error(false, "Multi line comment until end of file :/");
            return result;
        }
    }

    state->done += processed;
    processed = 0;

    #define HAS_CHARS(N) (state->done + processed + N < line_size)
    #define DATA(N) (data[state->done + processed + N])

    bool in_strliteral = false;
    bool in_charliteral = false;

    while (state->done + processed < line_size) {
        if (state->in_multiline_comment) {
            if (HAS_CHARS(1) && DATA(0) == '*' && DATA(1) == '/') {
                state->in_multiline_comment = false;
                state->column_start += 2;
                processed += 2;
            } else {
                state->column_start++;
                processed++;
            }
        }
        // Not in a multi line comment or string literal currently
        else if (!in_strliteral && !in_charliteral && !state->in_include) {
            // Let's check for single-line comments first.
            if (HAS_CHARS(1) && DATA(0) == '/' && DATA(1) == '/') {
                // Ok, we can just add a whitespace character and peace out.
                processed += 2;
                push_token(vec, state, &processed, PP_TOK_WHITESPACE);
                return result;
            }
            // Let's check for multi-line comments here.
            else if (HAS_CHARS(1) && DATA(0) == '/' && DATA(1) == '*') {
                state->in_multiline_comment = true;
                state->column_start += 2;
                processed += 2;
            }
            // Let's check for string literals.
            else if (DATA(0) == '"') {
                processed++;
                in_strliteral = true;
            }
            // And character literals.
            else if (DATA(0) == '\'') {
                processed++;
                in_charliteral = true;
            }
            // Let's skip whitespace
            else if (is_whitespace(DATA(0))) {
                processed++;
                while (HAS_CHARS(0) && is_whitespace(DATA(0))) { processed++; }
                push_token(vec, state, &processed, PP_TOK_WHITESPACE);
            }
            // Identifiers
            else if (is_ident_start(DATA(0))) {
                processed++;
                while (HAS_CHARS(0) && is_ident_char(DATA(0))) { processed++; }
                push_token(vec, state, &processed, PP_TOK_IDENTIFIER);
            }
            // Punctuators
            else if (DATA(0) == '#') {
                processed++;
                if (HAS_CHARS(0) && DATA(0) == '#') {
                    processed++;
                    push_token(vec, state, &processed, PP_TOK_DOUBLEHASH);
                } else {
                    push_token(vec, state, &processed, PP_TOK_HASH);
                }
            } else if (DATA(0) == '-') {
                processed++;
                if (HAS_CHARS(0) && DATA(0) == '>') {
                    processed++;
                    push_token(vec, state, &processed, PP_TOK_ARROW);
                } else if (HAS_CHARS(0) && DATA(0) == '-') {
                    processed++;
                    push_token(vec, state, &processed, PP_TOK_DECREMENT);
                } else if (HAS_CHARS(0) && DATA(0) == '=') {
                    processed++;
                    push_token(vec, state, &processed, PP_TOK_MINUS_ASSIGN);
                } else {
                    push_token(vec, state, &processed, PP_TOK_MINUS);
                }
            } else if (DATA(0) == ',') {
                processed++;
                push_token(vec, state, &processed, PP_TOK_COMMA);
            } else if (DATA(0) == '?') {
                processed++;
                push_token(vec, state, &processed, PP_TOK_QUESTION_MARK);
            } else if (DATA(0) == '=') {
                processed++;
                if (HAS_CHARS(0) && DATA(0) == '=') {
                    processed++;
                    push_token(vec, state, &processed, PP_TOK_EQUALS);
                } else {
                    push_token(vec, state, &processed, PP_TOK_ASSIGN);
                }
            } else if (DATA(0) == '+') {
                processed++;
                if (HAS_CHARS(0) && DATA(0) == '+') {
                    processed++;
                    push_token(vec, state, &processed, PP_TOK_INCREMENT);
                } else if (HAS_CHARS(0) && DATA(0) == '=') {
                    processed++;
                    push_token(vec, state, &processed, PP_TOK_PLUS_ASSIGN);
                } else {
                    push_token(vec, state, &processed, PP_TOK_PLUS);
                }
            } else if (DATA(0) == '*') {
                processed++;
                if (HAS_CHARS(0) && DATA(0) == '=') {
                    processed++;
                    push_token(vec, state, &processed, PP_TOK_STAR_ASSIGN);
                } else {
                    push_token(vec, state, &processed, PP_TOK_STAR);
                }
            } else if (DATA(0) == '/') {
                processed++;
                if (HAS_CHARS(0) && DATA(0) == '=') {
                    processed++;
                    push_token(vec, state, &processed, PP_TOK_DIV_ASSIGN);
                } else {
                    push_token(vec, state, &processed, PP_TOK_DIV);
                }
            } else if (DATA(0) == '%') {
                processed++;
                if (HAS_CHARS(0) && DATA(0) == '=') {
                    processed++;
                    push_token(vec, state, &processed, PP_TOK_MOD_ASSIGN);
                } else if (HAS_CHARS(0) && DATA(0) == '>') {
                    processed++;
                    push_token(vec, state, &processed, PP_TOK_CLOSE_BRACKET);
                } else if (HAS_CHARS(0) && DATA(0) == ':') {
                    processed++;
                    // Check if we have another one of those.
                    if (HAS_CHARS(1) && DATA(0) == '%' && DATA(1) == ':') {
                        processed += 2;
                        push_token(vec, state, &processed, PP_TOK_DOUBLEHASH);
                    } else if (HAS_CHARS(0) && DATA(0) == '#') {
                        processed++;
                        push_token(vec, state, &processed, PP_TOK_DOUBLEHASH);
                    } else {
                        push_token(vec, state, &processed, PP_TOK_HASH);
                    }
                } else {
                    push_token(vec, state, &processed, PP_TOK_MOD);
                }
            } else if (DATA(0) == '!') {
                processed++;
                if (HAS_CHARS(0) && DATA(0) == '=') {
                    processed++;
                    push_token(vec, state, &processed, PP_TOK_NOT_EQUALS);
                } else {
                    push_token(vec, state, &processed, PP_TOK_LOGICAL_NOT);
                }
            } else if (DATA(0) == '>') {
                // TODO: DIGRAPHS HERE
                processed++;
                if (HAS_CHARS(0) && DATA(0) == '=') {
                    processed++;
                    push_token(vec, state, &processed, PP_TOK_GREATER_EQUALS);
                } else if (HAS_CHARS(1) && DATA(0) == '>' && DATA(1) == '=') {
                    processed += 2;
                    push_token(vec, state, &processed, PP_TOK_RIGHT_SHIFT_ASSIGN);
                } else if (HAS_CHARS(0) && DATA(0) == '>') {
                    processed++;
                    push_token(vec, state, &processed, PP_TOK_RIGHT_SHIFT);
                } else {
                    push_token(vec, state, &processed, PP_TOK_GREATER);
                }
            } else if (DATA(0) == '<') {
                processed++;
                if (HAS_CHARS(0) && DATA(0) == '=') {
                    processed++;
                    push_token(vec, state, &processed, PP_TOK_LESS_EQUALS);
                } else if (HAS_CHARS(1) && DATA(0) == '<' && DATA(1) == '=') {
                    processed += 2;
                    push_token(vec, state, &processed, PP_TOK_LEFT_SHIFT_ASSIGN);
                } else if (HAS_CHARS(0) && DATA(0) == '<') {
                    processed++;
                    push_token(vec, state, &processed, PP_TOK_LEFT_SHIFT);
                } else {
                    push_token(vec, state, &processed, PP_TOK_LESS);
                }
            } else if (DATA(0) == '&') {
                processed++;
                if (HAS_CHARS(0) && DATA(0) == '=') {
                    processed++;
                    push_token(vec, state, &processed, PP_TOK_BITWISE_AND_ASSIGN);
                } else if (HAS_CHARS(0) && DATA(0) == '&') {
                    processed++;
                    push_token(vec, state, &processed, PP_TOK_LOGICAL_AND);
                } else {
                    push_token(vec, state, &processed, PP_TOK_BITWISE_AND);
                }
            } else if (DATA(0) == '|') {
                processed++;
                if (HAS_CHARS(0) && DATA(0) == '=') {
                    processed++;
                    push_token(vec, state, &processed, PP_TOK_BITWISE_OR_ASSIGN);
                } else if (HAS_CHARS(0) && DATA(0) == '|') {
                    processed++;
                    push_token(vec, state, &processed, PP_TOK_LOGICAL_OR);
                } else {
                    push_token(vec, state, &processed, PP_TOK_BITWISE_OR);
                }
            } else if (DATA(0) == '~') {
                processed++;
                push_token(vec, state, &processed, PP_TOK_BITWISE_NOT);
            } else if (DATA(0) == '^') {
                processed++;
                if (HAS_CHARS(0) && DATA(0) == '=') {
                    processed++;
                    push_token(vec, state, &processed, PP_TOK_BITWISE_XOR_ASSIGN);
                } else {
                    push_token(vec, state, &processed, PP_TOK_BITWISE_XOR);
                }
            } else if (DATA(0) == '[') {
                processed++;
                push_token(vec, state, &processed, PP_TOK_OPEN_SQUARE_BRACKET);
            } else if (DATA(0) == ']') {
                processed++;
                push_token(vec, state, &processed, PP_TOK_CLOSE_SQUARE_BRACKET);
            } else if (DATA(0) == '{') {
                processed++;
                push_token(vec, state, &processed, PP_TOK_OPEN_BRACKET);
            } else if (DATA(0) == '}') {
                processed++;
                push_token(vec, state, &processed, PP_TOK_CLOSE_BRACKET);
            } else if (DATA(0) == '(') {
                processed++;
                push_token(vec, state, &processed, PP_TOK_OPEN_PAREN);
            } else if (DATA(0) == ')') {
                processed++;
                push_token(vec, state, &processed, PP_TOK_CLOSE_PAREN);
            } else if (DATA(0) == ';') {
                processed++;
                push_token(vec, state, &processed, PP_TOK_SEMICOLON);
            } else if (DATA(0) == ':') {
                processed++;
                push_token(vec, state, &processed, PP_TOK_COLON);
            } else if (DATA(0) == '.') {
                processed++;
                if (HAS_CHARS(0) && isdigit(DATA(0))) {
                    goto do_number;
                } else {
                    push_token(vec, state, &processed, PP_TOK_DOT);
                }
            } else if (isdigit(DATA(0))) {
            do_number:
                processed++;
                while (HAS_CHARS(0) && (is_ident_char(DATA(0)) || DATA(0) == '.')) {
                    processed++;
                    if (DATA(-1) == 'e' || DATA(-1) == 'E' || DATA(-1) == 'p' || DATA(-1) == 'P') {
                        if (HAS_CHARS(0) && (DATA(0) == '+' || DATA(0) == '-')) {
                            processed++;
                        }
                    }
                }
                push_token(vec, state, &processed, PP_TOK_NUMBER);
            } else {
                printf("Unrecognized character with character code %d at line %lu, column %lu\n", DATA(0), state->line_start, state->column_start);
                processed++;
                push_token(vec, state, &processed, PP_TOK_OTHER);
            }
        } else if (in_strliteral) {
            if (HAS_CHARS(1) && DATA(0) == '\\' && DATA(1) == '"') {
                // Escaped quote.
                processed += 2;
            } else if (HAS_CHARS(1) && DATA(0) == '\\' && DATA(1) == '\\') {
                processed += 2;
            } else if (DATA(0) == '"') {
                // Finished with the string literal.
                processed++;
                // Push the token out.
                push_token(vec, state, &processed, PP_TOK_STR_LITERAL);
                in_strliteral = false;
            } else {
                processed++;
            }
        } else if (in_charliteral) {
            if (HAS_CHARS(1) && DATA(0) == '\\' && DATA(1) == '\'') {
                // Escaped tick
                processed += 2;
            } else if (HAS_CHARS(1) && DATA(0) == '\\' && DATA(1) == '\\') {
                processed += 2;
            } else if (DATA(0) == '\'') {
                processed++;
                push_token(vec, state, &processed, PP_TOK_CHAR_CONST);
                in_charliteral = false;
            } else {
                processed++;
            }
        } else if (state->in_include) {
            // TODO: Errors if we don't find closing character are broken (I think)
            // (Doesn't report some)

            // Let's skip some whitespace.
            if (is_whitespace(DATA(0))) {
                processed++;
                while (HAS_CHARS(0) && is_whitespace(DATA(0))) { processed++; }
                push_token(vec, state, &processed, PP_TOK_WHITESPACE);
            }

            state->in_include = false;

            if (HAS_CHARS(0)) {
                if (DATA(0) == '"') {
                    processed++;
                    while (HAS_CHARS(0)) {
                        if (DATA(0) == '"') {
                            // No quote escapes in header includes.
                            processed++;
                            push_token(vec, state, &processed, PP_TOK_HEADER_NAME);
                            break;
                        }
                        processed++;
                    }
                    if (!HAS_CHARS(0) && processed != 0) {
                        sc_error(false, "Relative include doesn't close in its line...");
                    }
                } else if (DATA(0) == '<') {
                    processed++;
                    while (HAS_CHARS(0)) {
                        if (DATA(0) == '>') {
                            processed++;
                            push_token(vec, state, &processed, PP_TOK_HEADER_NAME);
                            break;
                        }
                        processed++;
                    }
                    if (!HAS_CHARS(0) && processed != 0) {
                        sc_error(false, "Absolute include doesn't close in its line...");
                    }
                }
            }
        }
    }

    if (in_strliteral || in_charliteral) {
        sc_error(false, "In line %d, malformed %s literal (missing ending separator)", state->line_start, in_strliteral ? "string" : "character");
    }

    #undef DATA
    #undef HAS_CHARS

    return result;
}

void tokenizer_state_init(tokenizer_state *state, sc_file_cache_handle handle) {
    state->path = handle_to_file(handle)->abs_path;
    state->line_start = state->line_end = 1;
    state->column_start = state->column_end = 1;
    state->data = handle_to_file(handle)->contents;
    state->index = 0;
    state->data_size = handle_to_file(handle)->size;
    string_init(&state->current_data, 0);
    state->done = 0;
    state->in_multiline_comment = false;
    state->in_include = false;
}
