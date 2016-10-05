#ifndef PREPROCESSOR_H__
#define PREPROCESSOR_H__

#include <sc_alloc.h>
#include <tokenizer.h>

// TODO: Tune this to find a sweetspot.
#ifndef TOKEN_VECTOR_BLOCK_SIZE
    #define TOKEN_VECTOR_BLOCK_SIZE (4*1024)
#endif

typedef struct token_vector {
    token *memory;
    size_t size;
    size_t capacity;
} token_vector;

void token_vector_init(token_vector *vector);
// Copies the token into the token vector memory.
void token_vector_push(token_vector *vector, const token *token);
void token_vector_destroy(token_vector *vector);

void init_preprocessor(sc_path_table *table, sc_allocator *alloc);
void release_preprocessor();

// This is called recursively.
void preprocess(const char *file_path, token_vector *tok_vec);

#endif
