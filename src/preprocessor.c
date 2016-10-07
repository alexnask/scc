#include <preprocessor.h>
#include <macros.h>
#include <string.h>

sc_file_cache file_cache;
sc_path_table *default_paths;

static void preprocess_file(sc_file_cache_handle handle, preprocessing_state *state);

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
                // error
                // TODO: Split this off.
                skip_whitespace(current, state);

                char error[FILENAME_MAX];
                size_t written = 0;
                while (current->kind != TOK_NEWLINE && current->kind != TOK_EOF) {
                    char *current_str = zero_term_from_token(current);
                    long int current_size = token_size(current);
                    strncpy(error + written, current_str, current_size);
                    free(current_str);
                    written += current_size;
                    next_token(current, state);
                }
                error[written] = '\0';
                sc_error(false, "Error: %s", error);
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
                if (pre_state->if_nesting == 0) {
                    sc_error(false, "Dangling else directive (no condition before)...");
                    skip_to(current, state, TOK_NEWLINE);
                    return;       
                }

                // We were not ignoring, so let's do that.
                pre_state->ignoring = true;
                pre_state->ignore_until_nesting = pre_state->if_nesting - 1;

                skip_whitespace(current, state);
                if (current->kind != TOK_NEWLINE) {
                    sc_error(false, "Newline should follow #else directive...");
                    skip_to(current, state, TOK_NEWLINE);
                    return;
                }
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
            pre_state->if_nesting++;
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
