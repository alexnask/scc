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

    size_t left_index = 0;
    size_t right_index = 0;

    while (true) {
        // Skip whitespace of the left define, store if we did so.
        // Do the same for the right define then check wether the tokens and whitesapce are identical.
        // If one is done, they must both be done in addition to the token check.
        bool left_skipped = skip_whitespace(&left_index, &left->replacement_list);
        bool right_skipped = skip_whitespace(&right_index, &right->replacement_list);
    
        // If one of the two is done, too bad.
        if ((left_index == left_size) != (right_index == right_size)) return false;
        // For real, if we are done we should probably break out :p
        if (left_index == left_size || right_index == right_size) break;

        if (left_skipped != right_skipped) return false;
        if (!string_equals(&left_tokens[left_index].data, &right_tokens[right_index].data)) return false;
        left_index++;
        right_index++;
    }

    return true;
}

void do_define(size_t index, preprocessor_state *state) {
    pp_token_vector *vec = state->line_vec;
    pp_token *tokens = vec->memory;

    skip_whitespace(&index, vec);

    if (index == vec->size || tokens[index].kind != PP_TOK_IDENTIFIER) {
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
        bool skipped = skip_whitespace(&index, vec);
        if (tokens[index].kind == PP_TOK_OPEN_PAREN && !skipped) {
            // Function like macro.
            index++;

            // Read argument list
            macro_argument_decl *arg_decl = &new_def.args;

            bool first = true;
            while (index < vec->size && tokens[index].kind != PP_TOK_CLOSE_PAREN) {
                skip_whitespace(&index, vec);

                if (first) {
                    first = false;
                } else {
                    // Read comma.
                    if (index == vec->size || tokens[index].kind != PP_TOK_COMMA) {
                        sc_error(false, "Expected separating comma in argument declaration list of function like macro '%s'.",
                                 string_data(&new_def.define_name));
                        define_destroy(&new_def);
                        return;
                    }
                    index++;
                    skip_whitespace(&index, vec);
                }

                // Read argument.
                if (index == vec->size || (tokens[index].kind != PP_TOK_IDENTIFIER && tokens[index].kind != PP_TOK_DOT)) {
                    sc_error(false, "Expected argument name or varargs in argument declaration list of function like macro '%s'.",
                             string_data(&new_def.define_name));
                    define_destroy(&new_def);
                    return;
                }

                // Ok, we may have some varargs.
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

            // Skip closing parenthesis
            index++;

            // Check we have whitespace after closing paren.
            if (!skip_whitespace(&index, vec)) {
                sc_error(false, "Expected whitespace between function like macro argument list declaration and replacement list.");
                define_destroy(&new_def);
                return;
            }
        }

        // Write the replacement list!
        for (; index < vec->size; index++) {
            pp_token_vector_push(&new_def.replacement_list, &tokens[index]);
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
