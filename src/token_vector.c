#include <token_vector.h>

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

pp_token *pp_token_vector_tail(pp_token_vector *vector) {
    if (vector->size >= vector->capacity) {
        if (vector->capacity == 0) vector->capacity = 64;
        else vector->capacity *= 2;
        vector->memory = realloc(vector->memory, vector->capacity * sizeof(pp_token));
    }

    return &vector->memory[vector->size++];
}

void token_vector_init(token_vector *vector, size_t initial_capacity) {
    vector->size = 0;
    vector->capacity = initial_capacity;
    vector->memory = malloc(initial_capacity * sizeof(token));
}

void token_vector_push(token_vector *vector, const token *tok) {
    if (vector->size >= vector->capacity) {
        vector->capacity *= 2;
        vector->memory = realloc(vector->memory, vector->capacity * sizeof(token));
    }

    vector->memory[vector->size++] = *tok;
}

void token_vector_destroy(token_vector *vector) {
    if (vector->memory) {
        free(vector->memory);
        vector->memory = NULL;
    }
    vector->size = 0;
    vector->capacity = 0;
}

token *token_vector_tail(token_vector *vector) {
    if (vector->size >= vector->capacity) {
        vector->capacity *= 2;
        vector->memory = realloc(vector->memory, vector->capacity * sizeof(token));
    }

    return &vector->memory[vector->size++];
}
