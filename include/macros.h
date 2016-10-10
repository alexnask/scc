#ifndef MACROS_H__
#define MACROS_H__

#include <preprocessor.h>

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

bool macro_argument_decl_is_empty(macro_argument_decl *decl);
void macro_argument_decl_init_empty(macro_argument_decl *decl);
void macro_argument_decl_init(macro_argument_decl *decl);
bool macro_argument_decl_has(macro_argument_decl *decl, char *arg);
void macro_argument_decl_add(macro_argument_decl *decl, char *arg);
void macro_argument_decl_destroy(macro_argument_decl *decl);

typedef struct define {
    char *define_name;
    macro_argument_decl args;
    token_vector replacement_list;
    bool active;
} define;

void define_init_empty(define *def, char *define_name);
void define_destroy(define *def);

typedef struct define_table_t {
    define *defines;
    size_t define_count;
    size_t capacity;
} define_table_t;

void define_table_init();
define *define_table_lookup(char *def_name);
void define_table_add(define *def);
void define_table_destroy();

bool define_exists(char *def_name);

void add_define(preprocessing_state *state);
void do_define(preprocessing_state *state);
void do_undef(preprocessing_state *state);

#endif
