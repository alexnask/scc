#include <token_vector.h>

bool token_vector_is_empty(token_vector *vector) {
    return vector->size == 0;
}

void token_vector_init_empty(token_vector *vector) {
    vector->size = 0;
    vector->capacity = 0;
    vector->memory = NULL;
}

void token_vector_init(token_vector *vector, size_t initial_capacity) {
    vector->size = 0;
    vector->capacity = initial_capacity;
    vector->memory = malloc(initial_capacity * sizeof(token));
}

void token_vector_push(token_vector *vector, const token *tok) {
    if (vector->size >= vector->capacity) {
        if (vector->capacity == 0) vector->capacity = 64;
        else vector->capacity *= 2;
        vector->memory = realloc(vector->memory, vector->capacity * sizeof(token));
    }

    vector->memory[vector->size++] = *tok;
}

void token_vector_destroy(token_vector *vector) {
    free(vector->memory);
    vector->memory = NULL;
    vector->size = 0;
    vector->capacity = 0;
}
