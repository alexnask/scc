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

static void tokenizer_error(size_t index, size_t length, size_t line, size_t column, tokenizer_state *state, const char *error) {
    // So, we will take the pointer into the data and make a region of 10 characters to the left of it to 10 characters to the right of it.
    // If we hit a newline in those 10 characters, go until the newline (not including).
    // If we are not in whitespace after those 10 characters, go until whitespace or newline (unless we overflow buffer).
    const char *error_ptr = state->data + index;

    size_t start_off = index >= 10 ? 10 : index;
    size_t end_off = state->data_size - index - length >= 10 ? 10 : state->data_size - index - length;

    const char *start = error_ptr - start_off;
    const char *end = error_ptr + end_off;

    bool start_corrected = false;
    bool end_corrected = false;

    for (size_t i = start_off - 1; i >= 0; i--) {
        if (start[i] == '\n') {
            start = start + i + 1;
            start_off = error_ptr - start;
            start_corrected = true;
            break;
        }
    }

    for (size_t i = length; i <= length + end_off; i++) {
        if (error_ptr[i] == '\r' || error_ptr[i] == '\n') {
            end = error_ptr + i;
            end_off = i;
            end_corrected = true;
            break;
        }
    }

    if (!start_corrected) {
        // Ok, let's see where we point at.
        while (!is_whitespace(start[-1]) && start[-1] != '\n' && start_off <= index - 1) {
            start--;
            start_off++;
        }
    }

    if (!end_corrected) {
        while (!is_whitespace(end[1]) && end[1] != '\r' && end[1] != '\n' && end_off < (state->data_size - index)) {
            end++;
            end_off++;
        }
    }

    // So we write: [state->path]:[line]:[column]: Error: [error]\n
    //              [15 spaces][state->data[start_off : end_off]]\n
    //                               ~~~~~~~~

    // We assume that the numbers are 4 long at maximum
    size_t total_length = strlen(state->path) + 51 + strlen(error) + (end_off + start_off) + (index - start_off) + length;

    char space_buff[start_off + 1];
    memset(space_buff, ' ', start_off);
    space_buff[start_off] = '\0';

    char tilde_buff[length + 1];
    memset(tilde_buff, '~', length);
    tilde_buff[length] = '\0';

    char data_piece[end_off + start_off + 1];
    memcpy(data_piece, error_ptr - start_off, end_off + start_off);
    data_piece[end_off + start_off] = '\0';

    char buff[total_length + 1];
    snprintf(buff, total_length + 1, "%s:%lu:%lu: Error: %s\n               %s\n               %s%s\n",
             state->path, line, column, error, data_piece, space_buff, tilde_buff);

    // Just making sure.
    buff[total_length] = '\0';

    sc_error(false, buff);
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
        } else if (HAS_CHARS(2) && *data == '\\' && data[1] == '\r' && data[2] == '\n') {
            data += 3;
            state->index += 3;
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
    static pp_token_kind last_token_kind = PP_TOK_PLACEMARKER;

    pp_token *tok = pp_token_vector_tail(vec);
    if (*processed == 0) {
        string_init(&tok->data, 0);
    } else {
        substring(&tok->data, &state->current_data, state->done, state->done + *processed);
    }

    tok->kind = kind;

    if (last_token_kind == PP_TOK_HASH && kind == PP_TOK_IDENTIFIER) {
        if (STRING_EQUALS_LITERAL(&tok->data, "include")) {
            state->in_include = true;
        }
    }

    tok->source.path = state->path;
    tok->source.line = state->line_start;
    tok->source.column = state->column_start;

    tok->has_whitespace = false;

    state->column_start += *processed;

    state->done += *processed;
    *processed = 0;

    last_token_kind = kind;
}

bool tokenize_line(pp_token_vector *vec, tokenizer_state *state) {
    size_t original_vec_size = vec->size;
    // Get a processed line.
    bool result = get_processed_line(state);

    size_t line_size = string_size(&state->current_data);
    const char *data = string_data(&state->current_data);
    size_t processed = 0;

    #define GOT_WHITESPACE { state->done += processed; state->column_start += processed; processed = 0; \
                            if (vec->size > original_vec_size) { vec->memory[vec->size - 1].has_whitespace = true; } }

    // So, our line is ine "state->current_data"
    if (state->in_multiline_comment) {
        // We still are in some multiline comment, skip until we find the end (if we do)
        while (state->done + 1 < line_size) {
            if (data[state->done] == '*' && data[state->done + 1] == '/') {
                state->in_multiline_comment = false;
                processed += 2;
                GOT_WHITESPACE;
                break;
            }

            state->column_start++;
            state->done++;
        }

        if (state->in_multiline_comment && !result) {
            tokenizer_error(state->multiline_source.index, 2, state->multiline_source.line, state->multiline_source.column, state,
                            "Unterminated multi line comment.");
            return result;
        }
    }

    #define HAS_CHARS(N) (state->done + processed + N < line_size)
    #define DATA(N) (data[state->done + processed + N])

    bool in_strliteral = false;
    bool in_charliteral = false;

    while (state->done + processed < line_size) {
        if (state->in_multiline_comment) {
            if (HAS_CHARS(1) && DATA(0) == '*' && DATA(1) == '/') {
                state->in_multiline_comment = false;
                processed += 2;
                GOT_WHITESPACE;
                continue;
            } else {
                processed++;
            }
        }
        // Not in a multi line comment or string literal currently
        else if (!in_strliteral && !in_charliteral && !state->in_include) {
            // Let's check for single-line comments first.
            if (HAS_CHARS(1) && DATA(0) == '/' && DATA(1) == '/') {
                // Ok, we can just signal we got whitespace and peace out.
                processed += 2;
                GOT_WHITESPACE;
                return result;
            }
            // Let's check for multi-line comments here.
            else if (HAS_CHARS(1) && DATA(0) == '/' && DATA(1) == '*') {
                // Index into data to the start of the multiline comment.
                // Used for error reporting if the comment never ends.
                state->multiline_source.index = state->index - line_size + state->done + processed - 2;
                state->multiline_source.line = state->line_start;
                state->multiline_source.column = state->column_start;

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

                GOT_WHITESPACE;
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
            // Let's skip some whitespace.
            if (is_whitespace(DATA(0))) {
                processed++;
                while (HAS_CHARS(0) && is_whitespace(DATA(0))) { processed++; }

                GOT_WHITESPACE;
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
                        // TODO: Those 2's should be 1's if we have \n instead of \r\n (I think).
                        tokenizer_error(state->index - line_size + state->done - 2, processed,
                                        state->line_start, state->column_start, state, "Relative include not closed on its line.");
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
                        tokenizer_error(state->index - line_size + state->done - 2, processed,
                                        state->line_start, state->column_start, state, "Absolute include not closed on its line.");
                    }
                }
            }
        }
    }

    // Let's consider newlines as whitespaces.
    GOT_WHITESPACE;

    if (in_strliteral) {
        tokenizer_error(state->index - line_size + state->done - 2, processed,
                        state->line_start, state->column_start, state, "Unterminated string literal.");
    } else if (in_charliteral) {
        tokenizer_error(state->index - line_size + state->done - 2, processed,
                        state->line_start, state->column_start, state, "Unterminated character literal.");
    }

    #undef DATA
    #undef HAS_CHARS
    #undef GOT_WHITESPACE

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

bool pp_token_concatenate(pp_token *dest, pp_token *left, pp_token *right) {
    if (right->kind == PP_TOK_PLACEMARKER) {
        // This works even if both tokens are placemarkers!
        pp_token_copy(dest, left);
        return true;
    } else if (left->kind == PP_TOK_PLACEMARKER) {
        pp_token_copy(dest, right);
        return true;
    }

    if (left->kind == PP_TOK_IDENTIFIER) {
        if (right->kind != PP_TOK_IDENTIFIER && right->kind != PP_TOK_NUMBER) {
            return false;
        }

        // TODO: does this work with all number preprocessor tokens?
        pp_token_copy(dest, left);
        string_append(&dest->data, &right->data);
        return true;
    }

    if (left->kind == PP_TOK_HASH && right->kind == PP_TOK_HASH) {
        dest->kind = PP_TOK_CONCAT_DOUBLEHASH;
        dest->source = left->source;
        dest->has_whitespace = right->has_whitespace;
        STRING_FROM_LITERAL(&dest->data, "##");
        return true;
    }

    // TODO: Punctuators.

    return false;
}

void pp_token_copy(pp_token *dest, pp_token *src) {
    dest->kind = src->kind;
    dest->source = src->source;
    dest->has_whitespace = src->has_whitespace;
    string_copy(&dest->data, &src->data);
}
