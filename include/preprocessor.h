#ifndef PREPROCESSOR_H__
#define PREPROCESSOR_H__

#include <sc_alloc.h>
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

void init_preprocessor(sc_path_table *table, sc_allocator *alloc);
void release_preprocessor();

// This is called recursively.
void preprocess(const char *file_path, token_vector *tok_vec);

// TODO: Public interface for defines passed through -D

#endif
