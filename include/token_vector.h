#ifndef TOKEN_VECTOR_H__
#define TOKEN_VECTOR_H__

#include <tokenizer.h>

typedef struct pp_token_vector {
    pp_token *memory;
    size_t size;
    size_t capacity;
} pp_token_vector;

bool pp_token_vector_is_empty(pp_token_vector *vector);
void pp_token_vector_init_empty(pp_token_vector *vector);
void pp_token_vector_init(pp_token_vector *vector, size_t initial_capacity);
// Copies the token into the token vector memory.
void pp_token_vector_push(pp_token_vector *vector, const pp_token *token);
void pp_token_vector_destroy(pp_token_vector *vector);

void pp_token_vector_push_all(pp_token_vector *dest, const pp_token_vector * const src);

#endif
