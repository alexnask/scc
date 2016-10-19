#ifndef PREPROCESSOR_H__
#define PREPROCESSOR_H__

/*
TODO List:
- Write if, elif etc.
- Write include.
- User defined macros.
- Add a token source stack to pp_tokens, copy it over from the preprocessing state when we push tokens out to output vectors.
- Write nice error messages (like the tokenizer's) for the preprocessor (using the token source stack).

Future:
- Rewrite the preprocessor (at least macro handling) to be correct (at least every example of the C11 standard).
*/

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

    // Context for function like macro call across lines.
    struct {
        bool opened_call;
        define *macro;
        size_t nested_parentheses;
        size_t current_argument;
        pp_token *macro_ident;
        pp_token_vector *args;
    } macro_context;
} preprocessor_state;

void preprocessor_state_init(preprocessor_state *state, tokenizer_state *tok_state, token_vector *translation_unit, pp_token_vector *line_vec);

bool preprocess_line(preprocessor_state *state);

void preprocessor_clean_macro_context(preprocessor_state *state);

token_source *preprocessor_source_tail(preprocessor_state *state);
void preprocessor_pop_source(preprocessor_state *state);

// TODO: Public interface for defines passed through -D
// TODO: Builtin defines

#endif
