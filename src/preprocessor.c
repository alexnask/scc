#include <preprocessor.h>
#include <string.h>

// TODO: Check for EOL
static void skip_to(token *current, tokenizer_state *tok_state, token_kind kind) {
    do {
        next_token(current, tok_state);
    } while(current->kind != kind);
}

static char *zero_term_from_token(token *current) {
    char *buffer = malloc(current->source.size + 1);
    strncpy(buffer, handle_to_file(current->source.source_file)->contents + current->source.offset, current->source.size);
    buffer[current->source.size] = '\0';
    return buffer;
}

static char *unescape(const char *start, long int len) {
    // Do the actual unescaping you lazy sod.
    char *str = malloc(len + 1);
    strncpy(str, start, len);
    str[len] = '\0';
    return str;
}

static bool tok_str_cmp(token *current, const char *str) {
    size_t len = strlen(str);
    if (current->source.size != len) return false;

    for (size_t i = 0; i < len; ++i) {
        if (handle_to_file(current->source.source_file)->contents[current->source.offset + i] != str[i]) return false;
    }
    return true;
}

// Preprocessing tokens.
typedef enum pp_token_kind {
    PP_BUILTIN,
    PP_PLACEMARKER
} pp_token_kind;

typedef struct pp_token {
    pp_token_kind kind;
    token token;
    bool fully_substituted;
} pp_token;

#ifndef PP_TOKEN_VECTOR_BLOCK_SIZE
    #define PP_TOKEN_VECTOR_BLOCK_SIZE 128
#endif

typedef struct pp_token_vector {
    pp_token *memory;
    size_t size;
    size_t capacity;
} pp_token_vector;

static void pp_token_vector_init(pp_token_vector *vec) {
    vec->size = 0;
    vec->capacity = PP_TOKEN_VECTOR_BLOCK_SIZE;
    vec->memory = malloc(PP_TOKEN_VECTOR_BLOCK_SIZE * sizeof(pp_token));
}

static void pp_token_vector_push(pp_token_vector *vec, const pp_token *tok) {
    if (vec->size >= vec->capacity) {
        vec->capacity += PP_TOKEN_VECTOR_BLOCK_SIZE;
        vec->memory = realloc(vec->memory, vec->capacity * sizeof(pp_token));
    }

    vec->memory[vec->size++] = *tok;
}

static void pp_token_vector_destroy(pp_token_vector *vec) {
    free(vec->memory);
    vec->memory = NULL;
    vec->size = 0;
    vec->capacity = 0;
}

sc_file_cache file_cache;
sc_path_table *default_paths;

pp_token_vector buff1;
pp_token_vector buff2;

void token_vector_init(token_vector *vector) {
    vector->size = 0;
    vector->capacity = TOKEN_VECTOR_BLOCK_SIZE;
    vector->memory = malloc(TOKEN_VECTOR_BLOCK_SIZE * sizeof(token));
}

void token_vector_push(token_vector *vector, const token *tok) {
    if (vector->size >= vector->capacity) {
        vector->capacity += TOKEN_VECTOR_BLOCK_SIZE;
        vector->memory = realloc(vector->memory, vector->capacity * sizeof(token));
    }

    vector->memory[vector->size++] = *tok;
}

void token_vector_destroy(token_vector *vector) {
    free(vector->memory);
    vector->memory = NULL;
    vector->size = 0;
    vector->capacity = 0;
}

void init_preprocessor(sc_path_table *table, sc_allocator *alloc) {
    file_cache_init(&file_cache, alloc);
    default_paths = table;

    pp_token_vector_init(&buff1);
    pp_token_vector_init(&buff2);

    sc_enter_stage("preprocessing");
}

void release_preprocessor() {
    // TODO: The file cache destroy should be in another function (release_files or something like that).
    // We can destroy those two vectors but we have to keep file memory around since the tokens point into it.
    pp_token_vector_destroy(&buff1);
    pp_token_vector_destroy(&buff2);
    file_cache_destroy(&file_cache);
}

// Skips whitespace + comments.
// Returns true if we skipped any whitespace.
static bool skip_whitespace(token *current, tokenizer_state *state) {
    next_token(current, state);
    if (current->kind != TOK_WHITESPACE && current->kind != TOK_COMMENT) {
        return false;
    }

    do {
        next_token(current, state);
    } while (current->kind == TOK_WHITESPACE || current->kind == TOK_COMMENT);

    return true;
}

typedef struct preprocessing_state {
    token *current;
    tokenizer_state *state;
    token_vector *tok_vec;

    // Set by #line
    int line_diff;
    const char *path_overwrite;
} preprocessing_state;

static bool concatenate_path_tokens(token *current, tokenizer_state *tok_state, char *out, size_t max_out_len) {
    assert(current->kind == TOK_LESSTHAN);

    // So, we need to read tokens until a TOK_GREATERTHAN or a newline.
    // If we get to a newline first, we error out by returning false.
    // We concatenate the contents of our tokens on the way there.
    // Comment tokens are treated like a single whitespace character.
    next_token(current, tok_state);

    size_t written = 0;

    #define CHECK_WRITE(LEN) if (written + LEN >= max_out_len) { return false; }

    while (current->kind != TOK_GREATERTHAN && current->kind != TOK_NEWLINE && current->kind != TOK_EOF) {
        switch (current->kind) {
            case TOK_COMMENT:
                CHECK_WRITE(1);
                out[written++] = ' ';
            break;
            default:
                CHECK_WRITE(current->source.size);
                strncpy(out + written, handle_to_file(current->source.source_file)->contents + current->source.offset, current->source.size);
                written += current->source.size;
            break;
        }

        next_token(current, tok_state);
    }

    CHECK_WRITE(1);
    out[written] = '\0';

    #undef CHECK_WRITE

    if (current->kind == TOK_NEWLINE || current->kind == TOK_NEWLINE) {
        return false;
    }

    return true;
}

static void do_include(preprocessing_state *state) {
    tokenizer_state *tok_state = state->state;
    token *current = state->current;

    // Let's skip over space.
    bool skipped_whitespace = skip_whitespace(current, tok_state);

    // Ok, we either have a <...> include, a "..." include or a include [pp tokens].
    switch (current->kind) {
        case TOK_STRINGLITERAL: {
            // Relative include.
            // Omit the quotes on purpose.
            sc_file_cache_handle new_handle = { NULL, 0 };

            sc_file* current_file = handle_to_file(tok_state->source_handle);
            char *relative_path = unescape(current_file->contents + current->source.offset + 1, current->source.size - 2);

            {
                char absolute_path[FILENAME_MAX];
                get_relative_path_from_file(handle_to_file(tok_state->source_handle)->abs_path, relative_path, absolute_path, FILENAME_MAX);
                new_handle = file_cache_load(&file_cache, absolute_path);
            }

            // Ok, we found the file!
            if (new_handle.cache) {
                free(relative_path);
                // Preprocess the include in place!
                // TODO: This is wasteful since we know the file is already in the cache.
                // Make preprocess point to a preprocess_with_handle which takes handle directly.
                preprocess(handle_to_file(new_handle)->abs_path, state->tok_vec);
            } else {
                sc_warning("Could not locate file '%s' relative to '%s', searching in include paths instead.", relative_path,
                            handle_to_file(tok_state->source_handle)->abs_path);

                {
                    char absolute_path[FILENAME_MAX];
                    if (path_table_lookup(default_paths, relative_path, absolute_path, FILENAME_MAX)) {
                        // Ok, we found the file in the include path instead.
                        new_handle = file_cache_load(&file_cache, absolute_path);
                    } else {
                        sc_error(false, "File '%s' not located in include paths either.", relative_path);
                        free(relative_path);
                        return;
                    }
                }
                assert(new_handle.cache != NULL);
                preprocess(handle_to_file(new_handle)->abs_path, state->tok_vec);
            }
        } break;
        case TOK_LESSTHAN: {
            // Absolute include.
            // Look up the path in our include path.
            // If it exists, preprocess it into our token vector.
            // Otherwise, error out.
            sc_file_cache_handle new_handle = { NULL, 0 };
            {
                char include_path[FILENAME_MAX];
                if (concatenate_path_tokens(current, tok_state, include_path, FILENAME_MAX)) {
                    // Ok, we concatenated properly.
                    char absolute_path[FILENAME_MAX];
                    if (path_table_lookup(default_paths, include_path, absolute_path, FILENAME_MAX)) {
                        // We found the file!
                        new_handle = file_cache_load(&file_cache, absolute_path);
                    } else {
                        sc_error(false, "Could not locate file '%s' in include path.", include_path);
                        return;
                    }
                } else {
                    sc_error(false, "Unexpected termination of include path.");
                    skip_to(current, tok_state, TOK_NEWLINE);
                    return;
                }
            }
            assert(new_handle.cache != NULL);
            preprocess(handle_to_file(new_handle)->abs_path, state->tok_vec);
        } break;
        default:
            // TODO
            if (!skipped_whitespace) {
                char *temp = zero_term_from_token(current);
                sc_error(false, "Invalid include term '%s'.", temp);
                free(temp);
                skip_to(current, tok_state, TOK_NEWLINE);
                return;
            }
        break;
    }
}

static void handle_directive(preprocessing_state *pre_state) {
    token *current = pre_state->current;
    tokenizer_state *state = pre_state->state;

    // Ok, so we have read the hash.
    // Let's skip whitespace/comments.
    skip_whitespace(current, state);
    // Let's read our next token and figure out what is going on
    switch (current->kind) {
        case TOK_NEWLINE:
            // Ok, just go on...
            return;
        break;
        case TOK_KEYWORD:
        case TOK_IDENTIFIER:
            if (tok_str_cmp(current, "include")) {
                do_include(pre_state);
            } else if (tok_str_cmp(current, "error")) {
                // Do error
            } else if (tok_str_cmp(current, "ifdef")) {
                // Do ifdef
            } else if (tok_str_cmp(current, "ifndef")) {
                // Do ifndef
            } else if (tok_str_cmp(current, "if")) {
                // Do if
            } else if (tok_str_cmp(current, "pragma")) {
                // Do pragma
            } else if (tok_str_cmp(current, "define")) {
                // Do define
            } else if (tok_str_cmp(current, "undef")) {
                // Do undef
            } else if (tok_str_cmp(current, "line")) {
                // Do line
            } else if (tok_str_cmp(current, "else")) {
                sc_error(false, "Found else preprocessor directive without an if/ifdef/ifndef.");
                skip_to(current, state, TOK_NEWLINE);
                return;
            } else if (tok_str_cmp(current, "elif")) {
                sc_error(false, "Found elif preprocessor directive without an if/ifdef/ifndef.");
                skip_to(current, state, TOK_NEWLINE);
                return;
            } else if (tok_str_cmp(current, "endif")) {
                sc_error(false, "Found endif preprocessor directive without an if/ifdef/ifndef.");
                skip_to(current, state, TOK_NEWLINE);
                return;
            } else {
                char *temp = zero_term_from_token(current);
                sc_error(false, "Invalid preprocessor directive '%s'.", temp);
                free(temp);
                skip_to(current, state, TOK_NEWLINE);
                return;
            }
        break;
        default: {
            // Invalid direcrtive
            char *temp = zero_term_from_token(current);
            sc_error(false, "Invalid preprocessor directive '%s'.", temp);
            free(temp);
            skip_to(current, state, TOK_NEWLINE);
            return;
        } break;
    }
}

// TODO: Better errors...
void preprocess(const char *file_path, token_vector *tok_vec) {
    tokenizer_state state;
    sc_file_cache_handle handle = file_cache_load(&file_cache, file_path);

    if (!handle.cache) {
        sc_error(false, "Could not load file '%s'.", file_path);
        return;
    }

    tokenizer_state_init(&state, handle);

    // Are we currently in a text line?
    // (a text line is a line where a preprocessing directive cannot appear.)
    bool text_line = false;
    token current;

    preprocessing_state pp_state = {
        .current = &current,
        .state = &state,
        .tok_vec = tok_vec,
        .line_diff = 0,
        .path_overwrite = NULL
    };

    do {
        next_token(&current, &state);

        // Apply any #line changes.
        current.source.line += pp_state.line_diff;
        current.source.path_overwrite = pp_state.path_overwrite;

        switch (current.kind) {
            case TOK_NEWLINE:
                text_line = false;
            break;
            case TOK_WHITESPACE:
            case TOK_COMMENT:
            break;
            case TOK_HASH:
                if (text_line) {
                    sc_error(false, "Found '#' on a text line :/");
                } else {
                    handle_directive(&pp_state);
                }
            break;
            default:
                text_line = true;
                token_vector_push(tok_vec, &current);
            break;
        }
    } while(current.kind != TOK_EOF);
}
