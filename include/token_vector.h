#ifndef TOKEN_VECTOR_H__
#define TOKEN_VECTOR_H__

#include <tokenizer.h>

typedef struct pp_token_vector {
    pp_token *memory;
    size_t size;
    size_t capacity;
} pp_token_vector;

void pp_token_vector_init_empty(pp_token_vector *vector);
void pp_token_vector_init(pp_token_vector *vector, size_t initial_capacity);
// Copies the token into the token vector memory.
void pp_token_vector_push(pp_token_vector *vector, const pp_token *token);
void pp_token_vector_destroy(pp_token_vector *vector);
// Gives a pointer to a new element to be constructed like the caller sees fit.
pp_token *pp_token_vector_tail(pp_token_vector *vector);

typedef struct token_vector {
    token *memory;
    size_t size;
    size_t capacity;
} token_vector;

void token_vector_init(token_vector *vector, size_t initial_capacity);
// Copies the token into the token vector memory.
void token_vector_push(token_vector *vector, const token *token);
void token_vector_destroy(token_vector *vector);
// Gives a pointer to a new element to be constructed like the caller sees fit.
token *token_vector_anchor(token_vector *vector);

#endif
