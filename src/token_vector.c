#include <token_vector.h>

bool pp_token_vector_is_empty(pp_token_vector *vector) {
    return vector->size == 0;
}

void pp_token_vector_init_empty(pp_token_vector *vector) {
    vector->size = 0;
    vector->capacity = 0;
    vector->memory = NULL;
}

void pp_token_vector_init(pp_token_vector *vector, size_t initial_capacity) {
    vector->size = 0;
    vector->capacity = initial_capacity;
    vector->memory = malloc(initial_capacity * sizeof(pp_token));
}

void pp_token_vector_push(pp_token_vector *vector, const pp_token *tok) {
    if (vector->size >= vector->capacity) {
        if (vector->capacity == 0) vector->capacity = 64;
        else vector->capacity *= 2;
        vector->memory = realloc(vector->memory, vector->capacity * sizeof(pp_token));
    }

    vector->memory[vector->size++] = *tok;
}

void pp_token_vector_destroy(pp_token_vector *vector) {
    if (vector->memory) {
        free(vector->memory);
        vector->memory = NULL;
    }
    vector->size = 0;
    vector->capacity = 0;
}

void pp_token_vector_push_all(pp_token_vector *dest, const pp_token_vector * const src) {
    // TODO: Do a memcpy instead (realloc dest if necessary)
    for (size_t i = 0; i < src->size; i++) {
        pp_token_vector_push(dest, &src->memory[i]);
    }
}
