#ifndef MACROS_H__
#define MACROS_H__

#include <strings.h>
#include <token_vector.h>

#ifndef MACRO_ARGUMENT_DECL_BLOCK_SIZE
    #define MACRO_ARGUMENT_DECL_BLOCK_SIZE 16
#endif

// Argument decls take ownership of the argument tokens' data.
typedef struct macro_argument_decl {
    string *arguments;

    // Does not count the "varargs" argument.
    size_t argument_count;
    bool has_varargs;
    size_t capacity;
} macro_argument_decl;

bool macro_argument_decl_is_empty(macro_argument_decl *decl);
void macro_argument_decl_init_empty(macro_argument_decl *decl);
void macro_argument_decl_init(macro_argument_decl *decl);
bool macro_argument_decl_has(macro_argument_decl *decl, string *arg);
void macro_argument_decl_add(macro_argument_decl *decl, string *arg);
void macro_argument_decl_destroy(macro_argument_decl *decl);

// Defines take ownership of the name token's data.
// On correct redefinitions, destroy the redefinitions' strings. (as well as trhe args strings)
typedef struct define {
    string define_name;
    macro_argument_decl args;
    pp_token_vector replacement_list;
    bool active;

    struct {
        string path;
        size_t line;
        size_t column;
    } source;
} define;

void define_init_empty(define *def, string *define_name);
void define_destroy(define *def);

typedef struct define_table {
    define *defines;
    size_t define_count;
    size_t capacity;
} define_table;

void define_table_init(define_table *table);
define *define_table_lookup(define_table *table, string *def_name);
void define_table_add(define_table *table, define *def);
void define_table_destroy(define_table *table);

bool define_exists(define_table *table, string *def_name);

struct preprocessor_state;
void do_define(size_t index, struct preprocessor_state *state);
void macro_substitution(size_t index, struct preprocessor_state *state, pp_token_vector *out);

void continue_multiline_macro_function_call(struct preprocessor_state *state, size_t *index, pp_token_vector *out);

#endif
