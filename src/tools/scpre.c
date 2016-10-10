// The SCC preprocessor as an executable.
#include <preprocessor.h>

#include <stdio.h>

// TODO:
// - Finish the preprocessor
// (Replacement, builtins, #line, pragmas, #if)
// - Get rid of all size_t's
// - Look into doing small structure optimization on the vectors
// - Better tokens (multiple sources so that we can trace back to the origin through macros and such)
// - Improve errors (take a token in the preprocessor, pointer to data in tokenizer).
// - Create a small string optimized string, use that everywhere right from the start (copy file data into those, put it in token).
// (Most (if not all) tokens will fit into the small string, so we will have no extra overhead)
// - Go on to parser and static analyzer.

int main(int argc, char *argv[]) {
    // Just assume first argument passed is a file for now.
    if (argc < 3) {
        printf("Usage: %s <input file> <output file>\n", argv[0]);
        return 0;
    }

    char *in_path = argv[1];

    token_vector translation_unit;
    token_vector_init(&translation_unit, 2 * 1024);

    sc_path_table default_paths;
    path_table_init(&default_paths);
    path_table_add(&default_paths, "include");

    init_preprocessor(&default_paths, mallocator());

    preprocess(in_path, &translation_unit);

    sc_debug("Preprocessor returned translation unit %d tokens long.", translation_unit.size);

    char *out_path = argv[2];
    FILE *out_file = fopen(out_path, "w");

    for (size_t i = 0; i < translation_unit.size; i++) {
        token *current = &translation_unit.memory[i];
        fwrite(token_data(current), 1, token_size(current), out_file);
    }

    fclose(out_file);

    sc_enter_stage("cleaning up");
    token_vector_destroy(&translation_unit);
    release_preprocessor();

    return 0;
}
