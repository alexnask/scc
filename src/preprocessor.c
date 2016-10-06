#include <preprocessor.h>
#include <string.h>

// TODO: Check for EOL
static void skip_to(token *current, tokenizer_state *tok_state, token_kind kind) {
    do {
        next_token(current, tok_state);
    } while(current->kind != kind);
}

static char *zero_term_from_token(token *current) {
    long int tok_size = token_size(current);
    char *buffer = malloc(tok_size + 1);
    strncpy(buffer, token_data(current), tok_size);
    buffer[tok_size] = '\0';
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
    if (token_size(current) != len) return false;

    char *tok_data = token_data(current);

    for (size_t i = 0; i < len; ++i) {
        if (tok_data[i] != str[i]) return false;
    }
    return true;
}

#ifndef MACRO_ARGUMENT_DECL_BLOCK_SIZE
    #define MACRO_ARGUMENT_DECL_BLOCK_SIZE 16
#endif

typedef struct macro_argument_decl {
    char **arguments;

    // Does not count the "varargs" argument.
    size_t argument_count;
    bool has_varargs;
    size_t capacity;
} macro_argument_decl;

void macro_argument_decl_init(macro_argument_decl *decl) {
    decl->capacity = MACRO_ARGUMENT_DECL_BLOCK_SIZE;
    decl->argument_count = 0;

    decl->arguments = malloc(decl->capacity * sizeof(char *));
    decl->has_varargs = false;
}

void macro_argument_decl_add(macro_argument_decl *decl, char *arg) {
    if (decl->argument_count >= decl->capacity) {
        decl->capacity += MACRO_ARGUMENT_DECL_BLOCK_SIZE;
        decl->arguments = realloc(decl->arguments, decl->capacity * sizeof(char *));
    }

    decl->arguments[decl->argument_count++] = arg;
}

void macro_argument_decl_destroy(macro_argument_decl *decl) {
    if (decl) {
        for (size_t i = 0; i < decl->argument_count; i++) {
            free(decl->arguments[i]);
        }
        free(decl->arguments);
    }
}

typedef struct define {
    char *define_name;
    macro_argument_decl args;
    token_vector replacement_list;
    bool active;
} define;

typedef struct define_table_t {
    define *defines;
    size_t define_count;
    size_t capacity;
} define_table_t;

define_table_t define_table;

void define_table_init() {
    define_table.define_count = 0;
    define_table.capacity = 64;
    define_table.defines = malloc(64 * sizeof(define));
}

define *define_table_lookup(char *def_name) {
    for (size_t i = 0; i < define_table.define_count; ++i) {
        if (!strcmp(define_table.defines[i].define_name, def_name)) {
            return &define_table.defines[i];
        }
    }

    return NULL;
}

// Only adds it if it exists but is currently inactive
// Or it doesn't exist.
void define_table_add(define *def) {
    define *old_def = define_table_lookup(def->define_name);

    if (old_def && old_def->active) {
        // Nuh-huh
        return;
    }

    if (old_def) {
        // Boom.
        *old_def = *def;
    } else {
        if (define_table.define_count >= define_table.capacity) {
            define_table.capacity *= 2;
            define_table.defines = realloc(define_table.defines, define_table.capacity * sizeof(define));
        }

        define_table.defines[define_table.define_count++] = *def;
    }
}

void define_table_destroy() {
    for (size_t i = 0; i < define_table.define_count; i++) {
        macro_argument_decl_destroy(&define_table.defines[i].args);
        free(define_table.defines[i].define_name);
        token_vector_destroy(&define_table.defines[i].replacement_list);
    }
}

sc_file_cache file_cache;
sc_path_table *default_paths;

// pp_token_vector buff1;
// pp_token_vector buff2;

void token_vector_init(token_vector *vector, size_t initial_capacity) {
    vector->size = 0;
    vector->capacity = initial_capacity;
    vector->memory = malloc(initial_capacity * sizeof(token));
}

void token_vector_push(token_vector *vector, const token *tok) {
    if (vector->size >= vector->capacity) {
        vector->capacity *= 2;
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

    define_table_init();

    // pp_token_vector_init(&buff1);
    // pp_token_vector_init(&buff2);

    sc_enter_stage("preprocessing");
}

void release_preprocessor() {
    // TODO: The file cache destroy should be in another function (release_files or something like that).
    // We can destroy those two vectors but we have to keep file memory around since the tokens point into it.
    define_table_destroy();
    // pp_token_vector_destroy(&buff1);
    // pp_token_vector_destroy(&buff2);
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
    tokenizer_state *tok_state;
    token_vector *tok_vec;
    size_t if_nesting;
    // Set by #line
    int line_diff;
    const char *path_overwrite;
} preprocessing_state;

static void preprocess_file(sc_file_cache_handle handle, preprocessing_state *state);

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
                CHECK_WRITE(token_size(current));
                strncpy(out + written, token_data(current), token_size(current));
                written += token_size(current);
            break;
        }

        next_token(current, tok_state);
    }

    CHECK_WRITE(1);
    out[written] = '\0';

    #undef CHECK_WRITE

    if (current->kind == TOK_NEWLINE || current->kind == TOK_EOF) {
        return false;
    }

    return true;
}

// Handle new definitions.
static void add_define(preprocessing_state *state) {
    tokenizer_state *tok_state = state->tok_state;
    token *current = state->current;

    char *define_name = zero_term_from_token(current);

    define *maybe_exists = define_table_lookup(define_name);
    bool exists = maybe_exists && maybe_exists->active;

    macro_argument_decl arg_decl;
    //macro_argument_decl_init(&arg_decl);

    bool had_whitespace = skip_whitespace(current, tok_state);
    if (!had_whitespace) {
        // Ok, we need to have an open paren here.
        if (!current->kind == TOK_OPENPAREN) {
            sc_error(false, "Need whitespace between object macro definition and replacement list.");
            free(define_name);
            skip_to(current, tok_state, TOK_NEWLINE);
            return;
        }

        // Handle function like macro definition here.
        // We can either directly have a close paren, have '...'
        // or an argument list with optional ... at the end.
        skip_whitespace(current, tok_state);
        if (current->kind != TOK_CLOSEPAREN) {
            // No closing paren, actually find our identifier (argument) list.
        }
    }

    // read replacement list here

    // check redefinition.

    // add to define table.
}

// Check #defines, call add_define
static void do_define(preprocessing_state *state) {
    tokenizer_state *tok_state = state->tok_state;
    token *current = state->current;

    bool skipped_whitespace = skip_whitespace(current, tok_state);
    if (!skipped_whitespace) {
        sc_error(false, "Expected whitespace after #define.");
        skip_to(current, tok_state, TOK_NEWLINE);
        return;
    }

    switch (current->kind) {
        case TOK_KEYWORD:
            sc_warning("Warning: defining over a keyword, be careful.");
        case TOK_IDENTIFIER:
            add_define(state);
        break;
        default:
            sc_error(false, "Expected identifier after #define.");
            skip_to(current, tok_state, TOK_NEWLINE);
            return;
        break;
    }
}

static void do_include(preprocessing_state *state) {
    tokenizer_state *tok_state = state->tok_state;
    token *current = state->current;

    // Let's skip over space.
    bool skipped_whitespace = skip_whitespace(current, tok_state);

    // Ok, we either have a <...> include, a "..." include or a include [pp tokens].
    switch (current->kind) {
        case TOK_STRINGLITERAL: {
            token_source str_literal_source = current->source;
            // Ok, we need to have a newline right after (excluding whitespace).
            skip_whitespace(current, tok_state);
            if (current->kind != TOK_NEWLINE) {
                sc_error(false, "Expected newline after #include \"...\".");
                skip_to(current, tok_state, TOK_NEWLINE);
                return;
            }

            // Relative include.
            // Omit the quotes on purpose.
            sc_file_cache_handle new_handle = { NULL, 0 };

            sc_file* current_file = handle_to_file(tok_state->source_handle);
            char *relative_path = unescape(current_file->contents + str_literal_source.offset + 1, str_literal_source.size - 2);

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
                preprocess_file(new_handle, state);
            } else {
                sc_warning("Could not locate file '%s' relative to '%s', searching in include paths instead.", relative_path,
                            handle_to_file(tok_state->source_handle)->abs_path);

                {
                    char absolute_path[FILENAME_MAX];
                    if (path_table_lookup(default_paths, relative_path, absolute_path, FILENAME_MAX)) {
                        // Ok, we found the file in the include path instead.
                        new_handle = file_cache_load(&file_cache, absolute_path);
                        free(relative_path);
                    } else {
                        sc_error(false, "File '%s' not located in include paths either.", relative_path);
                        free(relative_path);
                        return;
                    }
                }
                assert(new_handle.cache != NULL);
                preprocess_file(new_handle, state);
            }
        } break;
        // TODO: <MACRO(HAHA)>
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
                    // We need to have a newline next.
                    skip_whitespace(current, tok_state);
                    if (current->kind != TOK_NEWLINE) {
                        sc_error(false, "Expected newline after #include <...>.");
                        skip_to(current, tok_state, TOK_NEWLINE);
                        return;
                    }

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
            preprocess_file(new_handle, state);
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
    tokenizer_state *state = pre_state->tok_state;

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
                do_define(pre_state);
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

static void do_preprocessing(preprocessing_state *state) {
    // Are we currently in a text line?
    // (a text line is a line where a preprocessing directive cannot appear.)
    bool text_line = false;

    do {
        next_token(state->current, state->tok_state);

        // Apply any #line changes.
        state->current->source.line += state->line_diff;
        state->current->source.path_overwrite = state->path_overwrite;

        // Check for text line state.
        switch (state->current->kind) {
            case TOK_NEWLINE:
                text_line = false;
            break;
            case TOK_WHITESPACE:
            case TOK_COMMENT:
            case TOK_HASH:
            break;
            default:
                text_line = true;
            break;
        }

        // Do work.
        // TODO: Check for macro substitution
        switch (state->current->kind) {
            case TOK_HASH:
                if (text_line) {
                    sc_error(false, "Found '#' on a text line :/");
                } else {
                    handle_directive(state);
                }
            break;
            case TOK_EOF:
            break;
            default:
                token_vector_push(state->tok_vec, state->current);
            break;
        }
    } while(state->current->kind != TOK_EOF);
}

static void preprocess_file(sc_file_cache_handle handle, preprocessing_state *state) {
    tokenizer_state tok_state;
    tokenizer_state_init(&tok_state, handle);

    token current;

    preprocessing_state new_pp_state = {
        .current = &current,
        .tok_state = &tok_state,
        .tok_vec = state->tok_vec,
        .if_nesting = state->if_nesting,
        // Those are re initialized since we are in a new file.
        .line_diff = 0,
        .path_overwrite = NULL
    };

    do_preprocessing(&new_pp_state);

    // Ok, we need to pass the current nesting level to our parent.
    state->if_nesting = new_pp_state.if_nesting;
}

// TODO: Better errors...
void preprocess(const char *file_path, token_vector *tok_vec) {
    tokenizer_state tok_state;
    sc_file_cache_handle handle = file_cache_load(&file_cache, file_path);

    if (!handle.cache) {
        sc_error(false, "Could not load file '%s'.", file_path);
        return;
    }

    tokenizer_state_init(&tok_state, handle);

    token current;

    preprocessing_state pp_state = {
        .current = &current,
        .tok_state = &tok_state,
        .tok_vec = tok_vec,
        .if_nesting = 0,
        .line_diff = 0,
        .path_overwrite = NULL
    };

    do_preprocessing(&pp_state);
}
