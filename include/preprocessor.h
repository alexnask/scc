#ifndef PREPROCESSOR_H__
#define PREPROCESSOR_H__

#include <sc_alloc.h>
#include <token_vector.h>

typedef struct preprocessing_state {
    token *current;
    tokenizer_state *tok_state;
    token_vector *tok_vec;
    size_t if_nesting;
    // When this is set to true, ignore_until_nesting indicates the level of nesting we are skipping up to.
    bool ignoring;
    size_t ignore_until_nesting;
    // Set by #line
    int line_diff;
    const char *path_overwrite;
} preprocessing_state;

void init_preprocessor(sc_path_table *table, sc_allocator *alloc);
void release_preprocessor();

// This is called recursively.
void preprocess(const char *file_path, token_vector *tok_vec);

// TODO: Public interface for defines passed through -D
// TODO: Builtin defines

#endif
