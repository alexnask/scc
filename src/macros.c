#include <macros.h>
#include <preprocessor.h>
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

    decl->arguments = malloc(decl->capacity * sizeof(string));
    decl->has_varargs = false;
}

bool macro_argument_decl_has(macro_argument_decl *decl, string *arg) {
    for (size_t i = 0; i < decl->argument_count; i++) {
        if (string_equals(&decl->arguments[i], arg))
            return true;
    }

    return false;
}

void macro_argument_decl_add(macro_argument_decl *decl, string *arg) {
    if (decl->argument_count >= decl->capacity) {
        decl->capacity += MACRO_ARGUMENT_DECL_BLOCK_SIZE;
        decl->arguments = realloc(decl->arguments, decl->capacity * sizeof(string));
    }

    string_copy(&decl->arguments[decl->argument_count++], arg);
}

void macro_argument_decl_destroy(macro_argument_decl *decl) {
    if (decl->arguments) {
        for (size_t i = 0; i < decl->argument_count; i++) {
            string_destroy(&decl->arguments[i]);
        }
        free(decl->arguments);
    }
}

void define_init_empty(define *def, string *define_name) {
    string_copy(&def->define_name, define_name);
    def->active = true; // active by default.
    macro_argument_decl_init_empty(&def->args);
    pp_token_vector_init_empty(&def->replacement_list);

    string_init(&def->source.path, 0);
    def->source.line = 0;
    def->source.column = 0;
}

void define_destroy(define *def) {
    string_destroy(&def->define_name);
    macro_argument_decl_destroy(&def->args);
    pp_token_vector_destroy(&def->replacement_list);
}

void define_table_init(define_table *table) {
    table->define_count = 0;
    table->capacity = 64;
    table->defines = malloc(64 * sizeof(define));
}

define *define_table_lookup(define_table *table, string *def_name) {
    for (size_t i = 0; i < table->define_count; ++i) {
        if (string_equals(&table->defines[i].define_name, def_name)) {
            return &table->defines[i];
        }
    }

    return NULL;
}

// Only adds it if it exists but is currently inactive
// Or it doesn't exist.
void define_table_add(define_table *table, define *def) {
    // Make sure the define we are adding is active.
    assert(def->active);

    define *old_def = define_table_lookup(table, &def->define_name);

    if (old_def && old_def->active) {
        assert(false);
        return;
    }

    if (old_def) {
        // Boom.
        define_destroy(old_def);
        *old_def = *def;
    } else {
        if (table->define_count >= table->capacity) {
            table->capacity *= 2;
            table->defines = realloc(table->defines, table->capacity * sizeof(define));
        }

        table->defines[table->define_count++] = *def;
    }
}

void define_table_destroy(define_table *table) {
    for (size_t i = 0; i < table->define_count; i++) {
        define_destroy(&table->defines[i]);
    }
    free(table->defines);
}

bool define_exists(define_table *table, string *def_name) {
    define *entry = define_table_lookup(table, def_name);

    return entry && entry->active;
}

static bool macro_defs_compatible(define *left, define *right) {
    assert(left->active && right->active);
    // Simple checks from declarations.
    if (left->args.has_varargs != right->args.has_varargs) return false;
    if (left->args.argument_count != right->args.argument_count) return false;
    // Check for spelling of arguments.
    for (size_t i = 0; i < left->args.argument_count; i++) {
        if (!string_equals(&left->args.arguments[i], &right->args.arguments[i])) {
            return false;
        }
    }

    // Check replacement lists.
    // We care about the tokens being the same, except for whitespace.
    // If we have whitespace in one list, we need to have whitespace in the other.
    pp_token *left_tokens = left->replacement_list.memory;
    pp_token *right_tokens = right->replacement_list.memory;

    size_t left_size = left->replacement_list.size;
    size_t right_size = right->replacement_list.size;

    // We can straight up compare the lengths of the lists since whitespaces are not tokens.
    if (left_size != right_size) return false;

    for (size_t i = 0; i < left_size; i++) {
        if (left_tokens[i].has_whitespace != right_tokens[i].has_whitespace) return false;
        if (!string_equals(&left_tokens[i].data, &right_tokens[i].data)) return false;
    }

    return true;
}

// TODO: Check for builtin redefinition.
void do_define(size_t index, preprocessor_state *state) {
    pp_token_vector *vec = state->line_vec;
    pp_token *tokens = vec->memory;

    if (tokens[index].kind != PP_TOK_IDENTIFIER) {
        sc_error(false, "Expected macro name after #define.");
        return;
    }

    define new_def;
    define_init_empty(&new_def, &tokens[index].data);

    string_from_ptr_size(&new_def.source.path, tokens[index].source.path, strlen(tokens[index].source.path));
    new_def.source.line = tokens[index].source.line;
    new_def.source.column = tokens[index].source.column;

    index++;
    if (index != vec->size) {
        // Object or function like macro.
        if (tokens[index].kind == PP_TOK_OPEN_PAREN && !tokens[index - 1].has_whitespace) {
            // Function like macro.
            index++;

            // Read argument list
            macro_argument_decl *arg_decl = &new_def.args;

            bool first = true;
            while (index < vec->size && tokens[index].kind != PP_TOK_CLOSE_PAREN) {
                if (first) {
                    first = false;
                } else {
                    // Read comma.
                    if (tokens[index].kind != PP_TOK_COMMA) {
                        sc_error(false, "Expected separating comma in argument declaration list of function like macro '%s'.",
                                 string_data(&new_def.define_name));
                        define_destroy(&new_def);
                        return;
                    }
                    index++;
                }

                // Read argument.
                if (tokens[index].kind != PP_TOK_IDENTIFIER && tokens[index].kind != PP_TOK_DOT) {
                    sc_error(false, "Expected argument name or varargs in argument declaration list of function like macro '%s'.",
                             string_data(&new_def.define_name));
                    define_destroy(&new_def);
                    return;
                }

                // Ok, we may have some varargs.
                // TODO: Check the dots don't have whitespace in between.
                if (tokens[index].kind == PP_TOK_DOT) {
                    bool error = false;
                    if (index >= vec->size - 2) {
                        error = true;
                    } else if (tokens[index + 1].kind != PP_TOK_DOT || tokens[index + 2].kind != PP_TOK_DOT) {
                        error = true;
                    } else{
                        if (arg_decl->has_varargs) {
                            sc_error(false, "Function like macro's '%s' argument list already contains a varargs parameter.",
                                     string_data(&new_def.define_name));
                            define_destroy(&new_def);
                            return;
                        } else {
                            index += 3;
                            arg_decl->has_varargs = true;
                        }
                    }

                    if (error) {
                        sc_error(false, "Expected argument name or varargs in argument declaration list of function like macro '%s'.",
                                 string_data(&new_def.define_name));
                        define_destroy(&new_def);
                        return;
                    }
                } else if (!arg_decl->has_varargs) {
                    // Add the argument name to the argument declaration list.
                    macro_argument_decl_add(arg_decl, &tokens[index].data);
                    index++;
                } else {
                    sc_error(false, "Trying to add argument to function like macro '%s' when varargs have already been defined.",
                             string_data(&new_def.define_name));
                    define_destroy(&new_def);
                    return;
                }
            }

            if (index == vec->size) {
                sc_error(false, "Function like macro argument list declaration does not end.");
                define_destroy(&new_def);
                return;
            }

            // Check we have whitespace after closing paren.
            // TODO: Something weird is happening here, index is incremented when adding arguments but back to the first argument here.
            // (Only tested with 1 arg)
            if (!tokens[index].has_whitespace) {
                sc_error(false, "Expected whitespace between function like macro '%s' argument list declaration and replacement list.",
                         string_data(&new_def.define_name));
                define_destroy(&new_def);
                return;
            }
            // Skip closing parenthesis
            index++;
        }

        // Write the replacement list!
        for (; index < vec->size; index++) {
            if (!new_def.args.has_varargs && STRING_EQUALS_LITERAL(&tokens[index].data, "__VA_ARGS__")) {
                sc_error(false, "The identifier __VA_ARGS__ can only appear in the replacement list of a function like variadic macro.");
                define_destroy(&new_def);
                return;
            }

            pp_token_vector_push(&new_def.replacement_list, &tokens[index]);
        }

        // TODO: Shouldn't those be non zero anyway?
        if (new_def.replacement_list.size > 0 && new_def.replacement_list.memory[0].kind == PP_TOK_DOUBLEHASH) {
            sc_error(false, "The '##' operator cannot appear in the first place of a macro replacement list.");
            define_destroy(&new_def);
            return;
        } else if (new_def.replacement_list.size > 0 && new_def.replacement_list.memory[new_def.replacement_list.size - 1].kind == PP_TOK_DOUBLEHASH) {
            sc_error(false, "The '##' operator cannot appear in the last place of a macro replacement list.");
            define_destroy(&new_def);
            return;
        }

        if (!macro_argument_decl_is_empty(&new_def.args)) {
            for (size_t i = 0; i < new_def.replacement_list.size; i++) {
                if (new_def.replacement_list.memory[i].kind == PP_TOK_HASH) {
                    if (i == new_def.replacement_list.size - 1) {
                        sc_error(false, "The '#' operator cannot appear in the last place of a function like macro replacement list.");
                        define_destroy(&new_def);
                        return;
                    }

                    i++;

                    if (new_def.replacement_list.memory[i].kind != PP_TOK_IDENTIFIER) {
                        sc_error(false, "The '#' operator must be followed by an argument identifier in a function like macro replacement list.");
                        define_destroy(&new_def);
                        return;
                    }

                    if (!macro_argument_decl_has(&new_def.args, &new_def.replacement_list.memory[i].data)
                        && !STRING_EQUALS_LITERAL(&new_def.replacement_list.memory[i].data, "__VA_ARGS__")) {
                        sc_error(false, "The '#' operator must be followed by an argument identifier in a function like macro replacement list.");
                        define_destroy(&new_def);
                        return;
                    }
                }
            }
        }
    }

    define *old_def = define_table_lookup(&state->def_table, &new_def.define_name);
    if (old_def && old_def->active) {
        // Check for redefinition, error + return on incompatible.
        if (!macro_defs_compatible(&new_def, old_def)) {
            // TODO: ERROR REPORTING
            // Show original definition + new definition.
            sc_error(false, "Incompatible redefinition of macro '%s'", string_data(&new_def.define_name));
        }

        define_destroy(&new_def);
    } else {
        define_table_add(&state->def_table, &new_def);
    }
}

// Fully substitutes all macros within the line_vec and pushes the resulting preprocessing tokens into a caller provided vector.
void macro_substitution(size_t index, preprocessor_state *state, pp_token_vector *out) {
    pp_token_vector *vec = state->line_vec;
    pp_token *tokens = vec->memory;

    // TODO: Function like macros
    // (Requires some additional state since the call could be across multiple lines)

    // TODO: Refactor this a bunch.

    for (; index < vec->size; index++) {
        if (tokens[index].kind == PP_TOK_IDENTIFIER) {
            define *macro = define_table_lookup(&state->def_table, &tokens[index].data);
            if (macro && macro->active) {
                // Add macro source to the stack.
                token_source *define_source = preprocessor_source_tail(state);
                define_source->kind = TSRC_MACRO;
                string_copy(&define_source->macro.name, &macro->define_name);
                define_source->macro.line = macro->source.line;
                define_source->macro.column = macro->source.column;

                if (macro_argument_decl_is_empty(&macro->args)) {
                    // Object like macro!
                    pp_token_vector temp_buff;
                    pp_token_vector_init(&temp_buff, 16);

                    // Copy the replacement list and do concatenations.
                    for (size_t i = 0; i < macro->replacement_list.size; i++) {
                        if (i < macro->replacement_list.size - 2 && macro->replacement_list.memory[i + 1].kind == PP_TOK_DOUBLEHASH) {
                            i += 2;
                            pp_token tmp_tok;
                            if (!pp_token_concatenate(&tmp_tok, &macro->replacement_list.memory[i - 2], &macro->replacement_list.memory[i])) {
                                sc_error(false, "Could not concatenate tokens '%s' and '%s'",
                                         string_data(&macro->replacement_list.memory[i - 2].data),
                                         string_data(&macro->replacement_list.memory[i].data));
                                continue;
                            }
                            pp_token_vector_push(&temp_buff, &tmp_tok);
                        }
                        else pp_token_vector_push(&temp_buff, &macro->replacement_list.memory[i]);
                    }

                    // TODO: Do more substitutions.

                    for (size_t i = 0; i < temp_buff.size; i++) {
                        pp_token_vector_push(out, &temp_buff.memory[i]);
                    }

                    pp_token_vector_destroy(&temp_buff);
                } else {
                    // Function like macro.
                }

                // Pop the macro source from the stack.
                preprocessor_pop_source(state);
                continue;
            }
        }
        pp_token_vector_push(out, &tokens[index]);
    }
}
