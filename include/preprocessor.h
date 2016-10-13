#ifndef PREPROCESSOR_H__
#define PREPROCESSOR_H__

#include <macros.h>
#include <token_vector.h>

typedef struct pp_branch {
    size_t nesting;
    bool ignoring;
} pp_branch;

typedef struct preprocessor_state {
    // This is switched then reset on #includes
    tokenizer_state *tok_state;
    token_vector *translation_unit;

    pp_token_vector *line_vec;

    struct {
        token_source *memory;
        size_t stack_size;
        size_t stack_capacity;
    } source_stack;

    size_t if_nesting;

    struct {
        pp_branch *memory;
        size_t size;
        size_t capacity;
    } branch_stack;

    define_table def_table;

    // Set by #line directive
    struct {
        string path;
        size_t line;
    } line;
} preprocessor_state;

void preprocessor_state_init(preprocessor_state *state, tokenizer_state *tok_state, token_vector *translation_unit, pp_token_vector *line_vec);

bool preprocess_line(preprocessor_state *state);

// TODO: Public interface for defines passed through -D
// TODO: Builtin defines

#endif
