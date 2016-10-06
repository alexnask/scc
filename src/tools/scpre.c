// The SCC preprocessor as an executable.
#include <preprocessor.h>

#include <stdio.h>

// TODO: add output file.

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
        char *data = handle_to_file(current->source.source_file)->contents + current->source.offset;
        fwrite(data, 1, current->source.size, out_file);
    }

    fclose(out_file);

    sc_enter_stage("cleaning up");
    token_vector_destroy(&translation_unit);
    release_preprocessor();

    return 0;
}
