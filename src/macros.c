#include <macros.h>

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


// Handle new definitions.
void add_define(preprocessing_state *state) {
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

