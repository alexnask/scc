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
