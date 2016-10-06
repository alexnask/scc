#ifndef TOKEN_VECTOR_H__
#define TOKEN_VECTOR_H__

#include <tokenizer.h>

typedef struct token_vector {
    token *memory;
    size_t size;
    size_t capacity;
} token_vector;

bool token_vector_is_empty(token_vector *vector);
void token_vector_init_empty(token_vector *vector);
void token_vector_init(token_vector *vector, size_t initial_capacity);
// Copies the token into the token vector memory.
void token_vector_push(token_vector *vector, const token *token);
void token_vector_destroy(token_vector *vector);

#endif
