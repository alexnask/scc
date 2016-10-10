#include <macros.h>
#include <string.h>

bool macro_argument_decl_is_empty(macro_argument_decl *decl) {
    return decl->argument_count == 0 && !decl->has_varargs;
}

// Note that we can add elements directly with 'macro_argument_decl_add'.
void macro_argument_decl_init_empty(macro_argument_decl *decl) {
    decl->arguments = NULL;
    decl->argument_count = 0;
    decl->capacity = 0;
    decl->has_varargs = false;
}

void macro_argument_decl_init(macro_argument_decl *decl) {
    decl->capacity = MACRO_ARGUMENT_DECL_BLOCK_SIZE;
    decl->argument_count = 0;

    decl->arguments = malloc(decl->capacity * sizeof(char *));
    decl->has_varargs = false;
}

bool macro_argument_decl_has(macro_argument_decl *decl, char *arg) {
    for (size_t i = 0; i < decl->argument_count; i++) {
        if (!strcmp(decl->arguments[i], arg)) return true;
    }

    return false;
}

void macro_argument_decl_add(macro_argument_decl *decl, char *arg) {
    if (decl->argument_count >= decl->capacity) {
        decl->capacity += MACRO_ARGUMENT_DECL_BLOCK_SIZE;
        decl->arguments = realloc(decl->arguments, decl->capacity * sizeof(char *));
    }

    decl->arguments[decl->argument_count++] = arg;
}

void macro_argument_decl_destroy(macro_argument_decl *decl) {
    if (decl->arguments) {
        for (size_t i = 0; i < decl->argument_count; i++) {
            free(decl->arguments[i]);
        }
        free(decl->arguments);
    }
}

void define_init_empty(define *def, char *define_name) {
    def->define_name = define_name;
    def->active = true; // active by default.
    macro_argument_decl_init_empty(&def->args);
    token_vector_init_empty(&def->replacement_list);
}

void define_destroy(define *def) {
    free(def->define_name);
    macro_argument_decl_destroy(&def->args);
    token_vector_destroy(&def->replacement_list);
}

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
    // Make sure the define we are adding is active.
    assert(def->active);

    define *old_def = define_table_lookup(def->define_name);

    if (old_def && old_def->active) {
        assert(false);
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

bool define_exists(char *def_name) {
    define *entry = define_table_lookup(def_name);

    return entry && entry->active;
}

static bool accumulate_spaces(token_vector *vec, size_t *space_count, size_t *index) {
    assert(*index < vec->size);

    token *tok = &vec->memory[*index];

    while (tok->kind == TOK_WHITESPACE || tok->kind == TOK_COMMENT) {
        if (tok->kind == TOK_WHITESPACE) {
            *space_count += token_size(tok);
        } else {
            assert(tok->kind == TOK_COMMENT);
            (*space_count)++;
        }

        (*index)++;
        tok++;
        assert(*index < vec->size);
    }

    return *index != vec->size - 1;
}

static bool defines_compatible(define *def1, define *def2) {
    // The rules are as follows:
    // For argument lists, we need to have the same number of arguments, the same vararg state and the same spelling of arguments.
    // For replacement lists, we need to have the exact same list (after converting any whitespace character to 1 space and comments to 1 space).
    bool args_empty = macro_argument_decl_is_empty(&def1->args);
    if (args_empty != macro_argument_decl_is_empty(&def2->args)) return false;

    bool repl_list_empty = token_vector_is_empty(&def1->replacement_list);
    if (repl_list_empty != token_vector_is_empty(&def2->replacement_list)) return false;

    if (!args_empty) {
        // Ok, we have arguments and need to check them.
        // Check varargs state first.
        if (def1->args.has_varargs != def2->args.has_varargs) return false;

        // Check argument count next.
        if (def1->args.argument_count != def2->args.argument_count) return false;
        // Check argument spelling.
        for (size_t i = 0; i < def1->args.argument_count; i++) {
            if (strcmp(def1->args.arguments[i], def2->args.arguments[i])) {
                return false;
            }
        }

        // Ok, arguments check out.
    }

    if (!repl_list_empty) {
        // We can't compare the lengths of the vectors since comments are still included here.
        // Instead, we keep an index into both of the vectors and an accumulated space count.
        // Whenever we hit a non space token we switch to the second vector and get to that point too and then compare space counts and tokens.
        size_t space_count1 = 0;
        size_t space_count2 = 0;
        size_t idx1 = 0;
        size_t idx2 = 0;

        while (true) {
            // Those return false when they've gone through the whole vector, so if we end at different iterations, the replacement lists are not equal.
            bool succ1 = accumulate_spaces(&def1->replacement_list, &space_count1, &idx1);
            bool succ2 = accumulate_spaces(&def2->replacement_list, &space_count2, &idx2);
            if (succ1 != succ2) return false;
            // If we've ended, the indexes point to the last element of the vector.
            // This should never be whitespace (should be skipped when making the replacement list).
            assert(def1->replacement_list.memory[idx1].kind != TOK_WHITESPACE && def1->replacement_list.memory[idx1].kind != TOK_COMMENT);
            assert(def2->replacement_list.memory[idx2].kind != TOK_WHITESPACE && def2->replacement_list.memory[idx2].kind != TOK_COMMENT);

            // Check that we have whitespace in both cases.
            if ((space_count1 != 0) != (space_count2 != 0)) return false;
            // Just check the tokens.
            if (!tok_cmp(&def1->replacement_list.memory[idx1], &def2->replacement_list.memory[idx2])) return false;

            idx1++;
            idx2++;

            if (!succ1) break;

            space_count1 = 0;
            space_count2 = 0;
        }
    }

    return true;
}

// Handle new definitions.
void add_define(preprocessing_state *state) {
    tokenizer_state *tok_state = state->tok_state;
    token *current = state->current;

    char *define_name = zero_term_from_token(current);
    bool had_whitespace = skip_whitespace(current, tok_state);

    // Tentative new define.
    define new_def;
    define_init_empty(&new_def, define_name);

    // If we don't have a simple define (#define MACRO_NAME whitespace newline), we need to figure out whether we are an object or function macro.
    if (current->kind != TOK_NEWLINE) {
        // If we didn't have whitespace, we should immediately be followed by an open parenthesis.
        // In that case, we are a function macro.
        // If we are immediately followed by a non parenthesis non whitespace token, the macro definition is invalid.
        if (!had_whitespace) {
            if (current->kind != TOK_OPENPAREN) {
                sc_error(false, "Need whitespace between object macro definition and replacement list.");
                free(define_name);
                return;
            }

            // Read argument list here.
            bool first = true;
            while (true) {
                skip_whitespace(current, tok_state);

                bool comma = false;

                if (current->kind == TOK_COMMA) {
                    if (first) {
                        sc_error(false, "Comma as a first token in function macro arguments.");
                        define_destroy(&new_def);
                        skip_to(current, tok_state, TOK_NEWLINE);
                        return;
                    }
                    skip_whitespace(current, tok_state);
                    comma = true;
                }

                if (!comma && current->kind == TOK_CLOSEPAREN) {
                    break;
                } else if (!comma && !first) {
                    sc_error(false, "Expected comma or closing parenthesis after function macro argument");
                    define_destroy(&new_def);
                    skip_to(current, tok_state, TOK_NEWLINE);
                    return;
                }

                first = false;

                if (new_def.args.has_varargs) {
                    sc_error(false, "Trying to add something to function macro arguments after varargs.");
                    define_destroy(&new_def);
                    skip_to(current, tok_state, TOK_NEWLINE);
                    return;
                }

                switch (current->kind) {
                    case TOK_KEYWORD: {
                        char *tok_str = zero_term_from_token(current);
                        sc_warning("Keyword '%s' in function macro argument list.", tok_str);
                        if (macro_argument_decl_has(&new_def.args, tok_str)) {
                            sc_error(false, "Defining argument '%s' of function macro '%s' twice.", tok_str);
                            free(tok_str);
                            define_destroy(&new_def);
                            skip_to(current, tok_state, TOK_NEWLINE);
                            return;
                        }
                        macro_argument_decl_add(&new_def.args, tok_str);
                    } break;
                    case TOK_IDENTIFIER: {
                        char *tok_str = zero_term_from_token(current);
                        if (macro_argument_decl_has(&new_def.args, tok_str)) {
                            sc_error(false, "Defining argument '%s' of function macro '%s' twice.", tok_str);
                            free(tok_str);
                            define_destroy(&new_def);
                            skip_to(current, tok_state, TOK_NEWLINE);
                            return;
                        }
                        macro_argument_decl_add(&new_def.args, tok_str);
                    } break;
                    case TOK_DOT: {
                        int dots = 1;
                        next_token(current, tok_state);
                        if (current->kind == TOK_DOT) {
                            dots++;
                            next_token(current, tok_state);
                            if (current->kind == TOK_DOT) {
                                dots++;
                            }
                        }

                        if (dots != 3) {
                            sc_error(false, "Got '%d' dots in function macro argument list, expected 3 (varargs).", dots);
                            define_destroy(&new_def);
                            skip_to(current, tok_state, TOK_NEWLINE);
                            return;
                        }

                        new_def.args.has_varargs = true;
                    } break;
                    default: {
                        char *tok_str = zero_term_from_token(current);
                        sc_error(false, "Unexpected token '%s' in function macro argument list.", tok_str);
                        free(tok_str);
                        define_destroy(&new_def);
                        skip_to(current, tok_state, TOK_NEWLINE);
                        return;
                    } break;
                }
            }
            // Check for close paren here, then skip whitespace.
            assert(current->kind == TOK_CLOSEPAREN);
            skip_whitespace(current, tok_state);
        }
    }

    if (current->kind != TOK_NEWLINE) {
        // Read replacement list here.
        do {
            if (tok_str_cmp(current, "__VA_ARGS__") && !new_def.args.has_varargs) {
                sc_error(false, "Identifier __VA_ARGS__ can only appear in vararg macros.");
                define_destroy(&new_def);
                skip_to(current, tok_state, TOK_NEWLINE);
                return;
            }

            token_vector_push(&new_def.replacement_list, current);
            next_token(current, tok_state);
        } while (current->kind != TOK_NEWLINE && current->kind != TOK_EOF);

        // We need to remove trailing whitespace.
        // TODO: split this into a token_vector_function?
        token *list_tok = &new_def.replacement_list.memory[new_def.replacement_list.size-1];
        while (list_tok->kind == TOK_WHITESPACE || list_tok->kind == TOK_COMMENT) {
            new_def.replacement_list.size--;
            list_tok--;
        }
    }

    assert(current->kind == TOK_NEWLINE);

    define *existing_entry = define_table_lookup(define_name);

    // Ok, we built up our macro spec, let's see if we can add it.
    if (!existing_entry || !existing_entry->active) {
        // Ok, we don't have such an entry, let's just add ours!
        define_table_add(&new_def);
    } else {
        // There is an entry with this name.
        // We need to see if it is compatible and error out if it is not.
        if (!defines_compatible(&new_def, existing_entry)) {
            sc_error(false, "Trying to redefine macro '%s' with incompatible definition.", define_name);
            define_destroy(&new_def);
        }
    }
}

// Check #defines, call add_define
void do_define(preprocessing_state *state) {
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
        break;
        default:
            sc_error(false, "Expected identifier after #define.");
            skip_to(current, tok_state, TOK_NEWLINE);
            return;
        break;
    }
}

void do_undef(preprocessing_state *state) {
    tokenizer_state *tok_state = state->tok_state;
    token *current = state->current;

    bool skipped_whitespace = skip_whitespace(current, tok_state);
    if (!skipped_whitespace) {
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

// Furthermore, if any nested replacements encounter the name of the macro being replaced, it is not replaced.
typedef struct replacement_context {
    // The original replacement list.
    // Will not be modified.
    const token_vector * const repl_list;
    // Output.
    token_vector *out;
    // Current macro we are replacing
    // This could be NULL if we are in a global context (i.e. replacing the arguments before replacing a macro function).
    const define * const macro;
    // Argument instances (a single varargs instance is provided, if necessary).
    // Could be null if we need no arguments.
    const token_vector * const args;
} replacement_context;

void do_replacement(replacement_context *ctx) {
    // Ok, let's start.
    token_vector temp_vec;
    token_vector_init(&temp_vec, 32);

    token_vector temp_vec_2;
    if (ctx->args != NULL) token_vector_init(&temp_vec_2, 32);
    else token_vector_init_empty(&temp_vec_2);

    // Stage 1: argument instance replacement.
    if (ctx->args != NULL) {
        size_t arg_count = ctx->macro->args.argument_count;
        bool has_varargs = ctx->macro->args.has_varargs;
        size_t true_count = has_varargs ? arg_count + 1 : arg_count;

        token_vector args_out[true_count];
        // Initialize our output vectors.
        for (size_t i = 0; i < true_count; ++i) {
            token_vector_init(&args_out[i], 16);
        }

        for (size_t i = 0; i < true_count; ++i) {
            replacement_context arg_ctx = {
                .repl_list = &ctx->args[i],
                .out = &args_out[i],
                .macro = NULL, // global context
                .args = NULL
            };

            do_replacement(&arg_ctx);
        }

        // Stage 2: argument substitution.
        for (size_t i = 0; i < ctx->repl_list->size; ++i) {
            token *current = &ctx->repl_list->memory[i];

            // TODO: These should skip whitespace
            #define PREVIOUS_IS(K) (i > 0 ? ctx->repl_list->memory[i - 1].kind == K : false)
            #define NEXT_IS(K) (i < ctx->repl_list->size - 1 ? ctx->repl_list->memory[i + 1].kind == K : false)
            #define CURR_IS(K) (ctx->repl_list->memory[i].kind == K)

            /*  A parameter in the replacement list, unless preceded by a # or ## preprocessing token or
                followed by a ## preprocessing token (see below), is replaced by the corresponding argument [...]
            */

            if ((CURR_IS(TOK_IDENTIFIER) || CURR_IS(TOK_KEYWORD)) && !PREVIOUS_IS(TOK_HASH) && !PREVIOUS_IS(TOK_DOUBLEHASH)
                 && !NEXT_IS(TOK_DOUBLEHASH)) {
                // Check if we need to replace with an argument.
                if (tok_str_cmp(current, "__VA_ARGS__")) {
                    // We know we have varargs, this cannot appear elsewhere.
                    assert(has_varargs);
                    // Dest, source
                    token_vector_push_all(&temp_vec, &args_out[arg_count]);
                } else {
                    bool found = false;
                    for (size_t i = 0; i < arg_count; i++) {
                        // Matched argument name, let's roll!
                        if (tok_str_cmp(current, ctx->macro->args.arguments[i])) {
                            found = true;
                            token_vector_push_all(&temp_vec, &args_out[i]);
                            break;
                        }
                    }

                    // If we didn't find an argument or varargs to substitute, just push the original token.
                    if (!found) token_vector_push(&temp_vec, current);
                }

                /*  If, in the replacement list of a function-like macro, a parameter is immediately preceded
                    or followed by a ## preprocessing token, the parameter is replaced by the corresponding
                    argument’s preprocessing token sequence; however, if an argument consists of no
                    preprocessing tokens, the parameter is replaced by a placemarker preprocessing token
                    instead.
                */
                // TODO: Figure out concatenation, should able to 1-pass #, ## (arg + non-arg)
                // # -> original token sequence in string
                // arg ## || ## arg -> placemarker if arg=[] else [expand arg] ## ...
                // ## expanded from args not executed
                // non-arg ## non-arg -> try to concatenate if it makes sense. (valid preprocessing token can be formed)
            } else {
                // Go ahead!
                token_vector_push(&temp_vec, current);
            }

            #undef CURR_IS
            #undef NEXT_IS
            #undef PREVIOUS_IS
        }

        for (size_t i = 0; i < arg_count; ++i) {
            token_vector_destroy(&args_out[i]);
        }
    } else {
        // We have no arguments, just push the replacement list into our temporary vector.
        token_vector_push_all(&temp_vec, ctx->repl_list);
    }

    /*  Each # preprocessing token in the replacement list for a function-like macro shall be
        followed by a parameter as the next preprocessing token in the replacement list.
    */

    // Stage 3: ## (for arguments)
    // Stage 4: # (for arguments)
    // Stage 5: ## (object + function macros) _NOT FROM ARGUMENT_
    // Stage 6: Remove placemarker tokens.
    // Stage 7: Rescan, excluding replacement of current macro_name (this applies even if we need to re-replace later).
    // Stage 8: _Pragma
    // Stage 9: Push to out.
    token_vector_destroy(&temp_vec_2);
    token_vector_destroy(&temp_vec);
}

// Called when our current token is an identifier or keyword.
// Returns whether we replaced the identifier.
bool macro_replace(preprocessing_state *state) {
    token *current = state->current;
    tokenizer_state *tok_state = state->tok_state;

    assert(current->kind == TOK_IDENTIFIER || current->kind == TOK_KEYWORD);

    // TODO: _Pragma

    char *macro_name = zero_term_from_token(current);
    define *entry = define_table_lookup(macro_name);
    if (!entry || !entry->active) {
        free(macro_name);
        return false;
    }
    free(macro_name);

    if (macro_argument_decl_is_empty(&entry->args)) {
        // Ok, object or simple macro.
        if (token_vector_is_empty(&entry->replacement_list)) {
            // Simple macro, nothing to replace.
            return true;
        }

        replacement_context ctx = {
            .repl_list = &entry->replacement_list,
            .out = state->tok_vec,
            .macro = entry,
            .args = NULL,
        };

        do_replacement(&ctx);
    }

    return true;
}
