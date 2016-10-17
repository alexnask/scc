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

// We see if such a macro exists and we aren't already substituting it
// If peek_stack is set, we look at the top of the source stack for a macro and do not substitute if we have the same name.
// Otherwise, we are working in a "global context" where we substitute any macro.
static define *should_substitute(preprocessor_state *state, string *name, bool peek_stack) {
    define *macro = define_table_lookup(&state->def_table, name);
    if (macro && macro->active) {
        if (peek_stack && state->source_stack.stack_size > 0) {
            token_source *top = &state->source_stack.memory[state->source_stack.stack_size - 1];
            if (top->kind == TSRC_MACRO && string_equals(name, &top->macro.name)) {
                return NULL;
            }
        }

        // Let's add the macro source to the source stack.
        token_source *new_source = preprocessor_source_tail(state);
        new_source->kind = TSRC_MACRO;
        string_copy(&new_source->macro.name, name);
        new_source->macro.line = macro->source.line;
        new_source->macro.column = macro->source.column;

        return macro;
    }

    return NULL;
}
static void object_macro_substitute(preprocessor_state *state, define *macro, pp_token_vector *out);
static void inline_function_macro_call(preprocessor_state *state, define *macro, pp_token_vector *in, size_t *i, pp_token_vector *out);

static void function_macro_substitute(preprocessor_state *state, define *macro, pp_token_vector *arguments, pp_token_vector *out) {
    size_t nargs = macro->args.argument_count;
    bool variadic = macro->args.has_varargs;
    // Create enough token vectors for our substituted arguments.
    pp_token_vector out_arguments[nargs + variadic ? 1 : 0];
    // Initialize them!
    for (size_t i = 0; i < nargs + variadic ? 1 : 0; i++) {
        pp_token_vector_init(&out_arguments[i], 8);
    }

    // Let's do the substitution!
    for (size_t i = 0; i < nargs + variadic ? 1 : 0; i++) {
        pp_token_vector *in_arg = &arguments[i];
        pp_token_vector *out_arg = &out_arguments[i];

        pp_token *in_toks = in_arg->memory;
        for (size_t j = 0; j < in_arg->size; j++) {
            if (in_toks[j].kind == PP_TOK_IDENTIFIER) {
                // We may need to substitute, in a global context.
                define *inside_macro = NULL;
                if ((inside_macro = should_substitute(state, &in_toks[j].data, false))) {
                    if (macro_argument_decl_is_empty(&inside_macro->args)) {
                        object_macro_substitute(state, inside_macro, out_arg);
                    } else {
                        if (j == in_arg->size - 1 || in_toks[j + 1].kind != PP_TOK_OPEN_PAREN) {
                            pp_token_vector_push(out_arg, &in_toks[j]);
                        } else {
                            // Ok, we need to read arguments and substitute the function like macro.
                            // Skip past the identifier and open paren tokens.
                            j += 2;
                            inline_function_macro_call(state, inside_macro, in_arg, &j, out_arg);
                        }
                    }

                    preprocessor_pop_source(state);
                } else pp_token_vector_push(out_arg, &in_toks[j]);
            } else pp_token_vector_push(out_arg, &in_toks[j]);
        }
    }

    // Ok, we've substituted all our arguments.
    // Let's go ahead and pull the replacement list and do the final substitutions.
    // TODO: LOTS of code duplication with object_macro_substitution
    for (size_t i = 0 ; i < macro->replacement_list.size; i++) {
        if (macro->replacement_list.memory[i].kind == PP_TOK_HASH) {
            i++;
            assert(i < macro->replacement_list.size);

            // Do '#' operator.
            size_t arg_index = 0;
            if (STRING_EQUALS_LITERAL(&macro->replacement_list.memory[i].data, "__VA_ARGS__")) {
                assert(variadic);
                arg_index = nargs;
            } else for (size_t arg_idx = 0; arg_idx < nargs; arg_idx++) {
                if (string_equals(&macro->replacement_list.memory[i].data, &macro->args.arguments[arg_idx])) {
                    arg_index = arg_idx;
                    break;
                }
            }

            // Ok, we have our argument index, we just have to make a string literal out of it and push it to 'out'.
            pp_token str_lit;
            str_lit.kind = PP_TOK_STR_LITERAL;
            str_lit.source = macro->replacement_list.memory[i - 1].source;
            str_lit.has_whitespace = true;
            string_init(&str_lit.data, 0);
            string_push(&str_lit.data, '"');
            for (size_t j = 0; j < out_arguments[arg_index].size; j++) {
                string_append(&str_lit.data, &out_arguments[arg_index].memory[j].data);
                if (out_arguments[arg_index].memory[j].has_whitespace) {
                    string_push(&str_lit.data, ' ');
                }
            }
            string_push(&str_lit.data, '"');
            // TODO: Escape string here.
            // Push the string literal out!
            pp_token_vector_push(out, &str_lit);
            // Skip the argument name
            i++;
        }
        // '##' operator.
        else if (i < macro->replacement_list.size - 2 && macro->replacement_list.memory[i + 1].kind == PP_TOK_DOUBLEHASH) {
            i += 2;
            pp_token tmp_tok;
            if (!pp_token_concatenate(&tmp_tok, &macro->replacement_list.memory[i - 2], &macro->replacement_list.memory[i])) {
                sc_error(false, "Could not concatenate tokens '%s' and '%s'",
                         string_data(&macro->replacement_list.memory[i - 2].data),
                         string_data(&macro->replacement_list.memory[i].data));
                continue;
            }
            pp_token_vector_push(out, &tmp_tok);
        } else if (macro->replacement_list.memory[i].kind == PP_TOK_IDENTIFIER) {
            // We may need to do a recursive substitution.
            define *inside_macro = NULL;
            if ((inside_macro = should_substitute(state, &macro->replacement_list.memory[i].data, true))) {
                if (macro_argument_decl_is_empty(&inside_macro->args)) {
                    // Another object macro, substitute it!
                    object_macro_substitute(state, inside_macro, out);
                } else {
                    // Function like macro.
                    // If it is not followed by a parenthesis, we just push the original token, since there can be no newline in a replacement list.
                    if (i == macro->replacement_list.size - 1 || macro->replacement_list.memory[i + 1].kind != PP_TOK_OPEN_PAREN) {
                        pp_token_vector_push(out, &macro->replacement_list.memory[i]);
                    } else {
                        // Ok, we need to read arguments and substitute the function like macro.

                        // Skip past the identifier and open paren tokens.
                        i += 2;
                        inline_function_macro_call(state, inside_macro, &macro->replacement_list, &i, out);
                    }
                }
                preprocessor_pop_source(state);
            } else pp_token_vector_push(out, &macro->replacement_list.memory[i]);
        }
        else pp_token_vector_push(out, &macro->replacement_list.memory[i]);
    }

    // Cleanup and return.
    for (size_t i = 0; i < nargs + variadic ? 1 : 0; i++) {
        pp_token_vector_destroy(&out_arguments[i]);
    }
}

static void inline_function_macro_call(preprocessor_state *state, define *macro, pp_token_vector *in, size_t *i, pp_token_vector *out) {
    // We get here after the opening parenthesis.
    size_t nested_paren = 0;
    pp_token *tokens = in->memory;

    // Create enough token vectors for our arguments.
    size_t nargs = macro->args.argument_count;
    bool variadic = macro->args.has_varargs;

    pp_token_vector arguments[nargs + variadic ? 1 : 0];
    // Initialize them!
    for (size_t i = 0; i < nargs + variadic ? 1 : 0; i++) {
        pp_token_vector_init(&arguments[i], 8);
    }

    size_t current_arg = 0;

    while (*i < in->size && (nested_paren != 0 || tokens[*i].kind != PP_TOK_CLOSE_PAREN)) {
        if (tokens[*i].kind == PP_TOK_OPEN_PAREN) {
            nested_paren++;
        } else if (tokens[*i].kind == PP_TOK_CLOSE_PAREN) {
            nested_paren--;
        } else if (nested_paren == 0 && tokens[*i].kind == PP_TOK_COMMA) {
            if (current_arg < nargs - 1) {
                current_arg++;
                (*i)++;
                continue;
            } else if (current_arg == nargs - 1) {
                if (variadic) {
                    current_arg++;
                    (*i)++;
                    continue;
                } else {
                    sc_error(false, "Trying to pass too many arguments to non variadic function like macro '%s'",
                             string_data(&macro->define_name));

                    goto cleanup_return;
                }
            }
            // If we have a comma after we got to the variadic argument, we commit it like everything else.
        }

        pp_token_vector_push(&arguments[current_arg], &tokens[(*i)++]);
    }

    if (*i == in->size || nested_paren != 0) {
        sc_error(false, "Malformed function like macro call.");
        goto cleanup_return;
    }

    // Skip past the closing parenthesis.
    assert(tokens[*i].kind == PP_TOK_CLOSE_PAREN);
    (*i)++;

    // Did we set all arguments?
    if (current_arg < nargs - 1) {
        sc_error(false, "Trying to pass too few arguments to function like macro '%s'",
                 string_data(&macro->define_name));
        goto cleanup_return;
    }

    // All ok! (I think?)
    // Actually substitute the macro.
    function_macro_substitute(state, macro, arguments, out);

cleanup_return:
    // Cleanup and return.
    for (size_t i = 0; i < nargs + variadic ? 1 : 0; i++) {
        pp_token_vector_destroy(&arguments[i]);
    }
}

static void object_macro_substitute(preprocessor_state *state, define *macro, pp_token_vector *out) {
    assert(macro_argument_decl_is_empty(&macro->args));

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
            pp_token_vector_push(out, &tmp_tok);
        } else if (macro->replacement_list.memory[i].kind == PP_TOK_IDENTIFIER) {
            // We may need to do a recursive substitution.
            define *inside_macro = NULL;
            if ((inside_macro = should_substitute(state, &macro->replacement_list.memory[i].data, true))) {
                if (macro_argument_decl_is_empty(&inside_macro->args)) {
                    // Another object macro, substitute it!
                    object_macro_substitute(state, inside_macro, out);
                } else {
                    // Function like macro.
                    // If it is not followed by a parenthesis, we just push the original token, since there can be no newline in a replacement list.
                    if (i == macro->replacement_list.size - 1 || macro->replacement_list.memory[i + 1].kind != PP_TOK_OPEN_PAREN) {
                        pp_token_vector_push(out, &macro->replacement_list.memory[i]);
                    } else {
                        // Ok, we need to read arguments and substitute the function like macro.

                        // Skip past the identifier and open paren tokens.
                        i += 2;
                        inline_function_macro_call(state, inside_macro, &macro->replacement_list, &i, out);
                    }
                }
                preprocessor_pop_source(state);
            } else pp_token_vector_push(out, &macro->replacement_list.memory[i]);
        }
        else pp_token_vector_push(out, &macro->replacement_list.memory[i]);
    }

    // TODO: Do recursive substitutions.
}

// TODO: Token sources don't actually end up in the final tokens, since they are popped back when we call the final push_token.
//       Add a token source stack to preprocessing tokens, push to it when copying preprocessing tokens out of substitution and finally to the final token.
// Fully substitutes all macros within the line_vec and pushes the resulting preprocessing tokens into a caller provided vector.
void macro_substitution(size_t index, preprocessor_state *state, pp_token_vector *out) {
    pp_token_vector *vec = state->line_vec;
    pp_token *tokens = vec->memory;

    for (; index < vec->size; index++) {
        if (tokens[index].kind == PP_TOK_IDENTIFIER) {
            define *macro = NULL;
            if ((macro = should_substitute(state, &tokens[index].data, true))) {
                if (macro_argument_decl_is_empty(&macro->args)) {
                    object_macro_substitute(state, macro, out);
                } else {
                    // Function like macro.
                    // This could be a call across lines, which makes things tricky.
                }

                preprocessor_pop_source(state);
                continue;
            }
        }
        pp_token_vector_push(out, &tokens[index]);
    }
}
