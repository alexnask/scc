#include <preprocessor.h>
#include <string.h>

typedef struct preprocessing_state {
    token *current;
    tokenizer_state *tok_state;
    token_vector *tok_vec;
    size_t if_nesting;
    // When this is set to true, ignore_until_nesting indicates the level of nesting we are skipping up to.
    bool ignoring;
    size_t ignore_until_nesting;
    // Set by #line
    int line_diff;
    const char *path_overwrite;
} preprocessing_state;

static void preprocess_file(sc_file_cache_handle handle, preprocessing_state *state);

static void skip_to(token *current, tokenizer_state *tok_state, token_kind kind) {
    do {
        next_token(current, tok_state);
    } while(current->kind != kind && current->kind != TOK_EOF);
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

static bool macro_argument_decl_is_empty(macro_argument_decl *decl) {
    return decl->argument_count == 0 && !decl->has_varargs;
}

// Note that we can add elements directly with 'macro_argument_decl_add'.
static void macro_argument_decl_init_empty(macro_argument_decl *decl) {
    decl->arguments = NULL;
    decl->argument_count = 0;
    decl->capacity = 0;
    decl->has_varargs = false;
}

static void macro_argument_decl_init(macro_argument_decl *decl) {
    decl->capacity = MACRO_ARGUMENT_DECL_BLOCK_SIZE;
    decl->argument_count = 0;

    decl->arguments = malloc(decl->capacity * sizeof(char *));
    decl->has_varargs = false;
}

static void macro_argument_decl_add(macro_argument_decl *decl, char *arg) {
    if (decl->argument_count >= decl->capacity) {
        decl->capacity += MACRO_ARGUMENT_DECL_BLOCK_SIZE;
        decl->arguments = realloc(decl->arguments, decl->capacity * sizeof(char *));
    }

    decl->arguments[decl->argument_count++] = arg;
}

static void macro_argument_decl_destroy(macro_argument_decl *decl) {
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

static void define_init_empty(define *def, char *define_name) {
    def->define_name = define_name;
    def->active = true; // active by default.
    macro_argument_decl_init_empty(&def->args);
    token_vector_init_empty(&def->replacement_list);
}

static void define_destroy(define *def) {
    free(def->define_name);
    macro_argument_decl_destroy(&def->args);
    token_vector_destroy(&def->replacement_list);
}

typedef struct define_table_t {
    define *defines;
    size_t define_count;
    size_t capacity;
} define_table_t;

define_table_t define_table;

static void define_table_init() {
    define_table.define_count = 0;
    define_table.capacity = 64;
    define_table.defines = malloc(64 * sizeof(define));
}

static define *define_table_lookup(char *def_name) {
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
    // Make sure the define we are adding is active.
    assert(def->active);

    define *old_def = define_table_lookup(def->define_name);

    if (old_def && old_def->active) {
        // Nuh-huh
        return;
    }

    if (old_def) {
        // Boom.
        define_destroy(old_def);
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
        define_destroy(&define_table.defines[i]);
    }
    free(define_table.defines);
}

sc_file_cache file_cache;
sc_path_table *default_paths;

bool token_vector_is_empty(token_vector *vector) {
    return vector->size == 0;
}

void token_vector_init_empty(token_vector *vector) {
    vector->size = 0;
    vector->capacity = 0;
    vector->memory = NULL;
}

void token_vector_init(token_vector *vector, size_t initial_capacity) {
    vector->size = 0;
    vector->capacity = initial_capacity;
    vector->memory = malloc(initial_capacity * sizeof(token));
}

void token_vector_push(token_vector *vector, const token *tok) {
    if (vector->size >= vector->capacity) {
        if (vector->capacity == 0) vector->capacity = 64;
        else vector->capacity *= 2;
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
    sc_enter_stage("preprocessing");
}

void release_preprocessor() {
    // TODO: The file cache destroy should be in another function (release_files or something like that).
    define_table_destroy();
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

    //define *maybe_exists = define_table_lookup(define_name);
    //bool exists = maybe_exists && maybe_exists->active;

    //macro_argument_decl arg_decl;
    //macro_argument_decl_init(&arg_decl);

    bool had_whitespace = skip_whitespace(current, tok_state);

    // Simple define (no replacement list)
    // Just #define SMTHING [whitespace]\n
    if (current->kind == TOK_NEWLINE) {
        // Ok, we just need to add that simple define.
        // If it already exists, we need to make sure it was defined with no arguments and replacement list.
        define *entry = define_table_lookup(define_name);

        if (entry && entry->active) {
            // Already exists, check stuff here.
            if (!macro_argument_decl_is_empty(&entry->args) || !token_vector_is_empty(&entry->replacement_list)) {
                // Wow...
                sc_error(false, "Trying to redefine an object or function macro as a simple macro.");
                free(define_name);
                return;
            }
        } else {
            define new_def;
            define_init_empty(&new_def, define_name);
            // Add our def!
            define_table_add(&new_def);
            return;
        }
    } else if (!had_whitespace) {
        // Ok, we need to have an open paren here.
        if (current->kind != TOK_OPENPAREN) {
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
        case TOK_KEYWORD: {
            char *keyword_str = zero_term_from_token(current);
            sc_warning("Warning: defining over keyword '%s', be careful.", keyword_str);
            free(keyword_str);
        }
        case TOK_IDENTIFIER:
            add_define(state);
            // TEMPORARY
            return;
        break;
        default:
            sc_error(false, "Expected identifier after #define.");
            skip_to(current, tok_state, TOK_NEWLINE);
            return;
        break;
    }
}

static void do_undef(preprocessing_state *state) {
    tokenizer_state *tok_state = state->tok_state;
    token *current = state->current;

    bool skipped_whitespace = skip_whitespace(current, tok_state);
    if (!skip_whitespace) {
        sc_error(false, "Expected whitespace after #undef.");
        skip_to(current, tok_state, TOK_NEWLINE);
        return;
    }

    switch (current->kind) {
        case TOK_KEYWORD:
        case TOK_IDENTIFIER: {
            char *define_name = zero_term_from_token(current);
            // We need a newline after whitespace.
            skip_whitespace(current, tok_state);
            if (current->kind != TOK_NEWLINE) {
                free(define_name);
                sc_error(false, "Newline should follow #undef directive.");
                return;
            }
            // Try to find our entry and set it to inactive.
            // If we don't find it no big deal.
            define *entry = define_table_lookup(define_name);
            if (entry) {
                entry->active = false;
            } else {
                sc_debug("Undef'd non-existant define '%s'.", define_name);
            }

            free(define_name);
        } break;
        default: {
            char *token_string = zero_term_from_token(current);
            sc_error(false, "Unexpected term '%s' in #undef directive.", token_string);
            skip_to(current, tok_state, TOK_NEWLINE);
            free(token_string);
        }
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

static void do_ifdef(bool should_be_defined, preprocessing_state *pre_state) {
    token *current = pre_state->current;
    tokenizer_state *tok_state = pre_state->tok_state;

    skip_whitespace(current, tok_state);
    if (current->kind != TOK_KEYWORD && current->kind != TOK_IDENTIFIER) {
        sc_error(false, "Expected identifier to check in #%s.", should_be_defined ? "ifdef" : "ifndef");
        skip_to(current, tok_state, TOK_NEWLINE);
        return;
    }

    // Ok, we have an identifier, figure it out.
    char *define_name = zero_term_from_token(current);

    skip_whitespace(current, tok_state);
    if (current->kind != TOK_NEWLINE) {
        free(define_name);
        sc_error(false, "Expected newline after #%s.", should_be_defined ? "ifdef" : "ifndef");
        skip_to(current, tok_state, TOK_NEWLINE);
        return;
    }

    // Look it up.
    define *entry = define_table_lookup(define_name);
    free(define_name);

    // First part: whether it exists
    // Second part: whether we want it to exist to fire the condition.
    if ((entry && entry->active) != should_be_defined) {
        // We need to ignore.
        pre_state->ignoring = true;
        pre_state->ignore_until_nesting = pre_state->if_nesting;
    }

    pre_state->if_nesting++;
}

static void handle_directive(preprocessing_state *pre_state) {
    token *current = pre_state->current;
    tokenizer_state *state = pre_state->tok_state;

    // Ok, so we have read the hash.
    // Let's skip whitespace/comments.
    skip_whitespace(current, state);
    // Let's read our next token and figure out what is going on
    if (!pre_state->ignoring) switch (current->kind) {
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
                do_ifdef(true, pre_state);
            } else if (tok_str_cmp(current, "ifndef")) {
                // Do ifndef
                do_ifdef(false, pre_state);
            } else if (tok_str_cmp(current, "if")) {
                // TODO;
                // Do if
                skip_to(current, state, TOK_NEWLINE);
            } else if (tok_str_cmp(current, "pragma")) {
                // Do pragma
            } else if (tok_str_cmp(current, "define")) {
                do_define(pre_state);
            } else if (tok_str_cmp(current, "undef")) {
                do_undef(pre_state);
            } else if (tok_str_cmp(current, "line")) {
                // Do line
            } else if (tok_str_cmp(current, "else")) {
                skip_to(current, state, TOK_NEWLINE);
            } else if (tok_str_cmp(current, "elif")) {
                skip_to(current, state, TOK_NEWLINE);
            } else if (tok_str_cmp(current, "endif")) {
                if (pre_state->if_nesting == 0) {
                    sc_error(false, "Trying to #endif when nesting level is already zero...");
                    skip_to(current, state, TOK_NEWLINE);
                    return;
                }
                pre_state->if_nesting--;
                skip_whitespace(current, state);
                if (current->kind != TOK_NEWLINE) {
                    sc_error(false, "Expected newline directly after #endif...");
                    skip_to(current, state, TOK_NEWLINE);
                    return;
                }
            } else {
                char *temp = zero_term_from_token(current);
                sc_error(false, "Invalid preprocessor directive '%s'.", temp);
                free(temp);
                skip_to(current, state, TOK_NEWLINE);
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
    } else if (current->kind == TOK_KEYWORD || current->kind == TOK_IDENTIFIER) {
        // TODO: Should we still check for invalid directives while in ignoring mode? Check the standard.
        if (tok_str_cmp(current, "ifdef") || tok_str_cmp(current, "ifndef") || tok_str_cmp(current, "if")) {
            pre_state->if_nesting += 1;
        } else if (tok_str_cmp(current, "else")) {
            if (pre_state->if_nesting == pre_state->ignore_until_nesting + 1) {
                // Ok, we must stop ignoring again, since we were ignoring until (N - 1) and we found an else at N.
                pre_state->ignoring = false;
                return;
            }
        } else if (tok_str_cmp(current, "elif")) {
            if (pre_state->if_nesting == pre_state->ignore_until_nesting + 1) {
                // TODO;
                // Figure out whether the elif condition is met and stop ignoring if it is, keep on otherwise. 
            }
        } else if (tok_str_cmp(current, "endif")) {
            pre_state->if_nesting--;
            if (pre_state->if_nesting == pre_state->ignore_until_nesting) {
                pre_state->ignoring = false;
            }

            skip_whitespace(current, state);
            if (current->kind != TOK_NEWLINE) {
                sc_error(false, "Expected newline directly after #endif...");
                skip_to(current, state, TOK_NEWLINE);
                return;
            }
        }
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
                // Ignoring will be set by 'handle_directives'.
                if (!state->ignoring) {
                    token_vector_push(state->tok_vec, state->current);
                }
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
        .ignoring = state->ignoring,
        .ignore_until_nesting = state->ignore_until_nesting,
        // Those are re initialized since we are in a new file.
        .line_diff = 0,
        .path_overwrite = NULL
    };

    do_preprocessing(&new_pp_state);

    // Ok, we need to pass the current nesting level to our parent.
    state->if_nesting = new_pp_state.if_nesting;
    state->ignoring = new_pp_state.ignoring;
    state->ignore_until_nesting = new_pp_state.ignore_until_nesting;
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
        .ignoring = false,
        .ignore_until_nesting = 0,
        .line_diff = 0,
        .path_overwrite = NULL
    };

    do_preprocessing(&pp_state);

    if (pp_state.if_nesting != 0) {
        sc_error(false, "Missing %d #endifs in translation unit.", pp_state.if_nesting);
    }
}
