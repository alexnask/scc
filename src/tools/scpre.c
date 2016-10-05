// The SCC preprocessor as an executable.
#include <preprocessor.h>

#include <stdio.h>

// TODO: add output file.

int main(int argc, char *argv[]) {
    // Just assume first argument passed is a file for now.
    if (argc < 2) {
        printf("No file supplied to scpre.\n");
        printf("Usage: %s <input file>\n", argv[0]);
        return 0;
    }

    char *file_path = argv[1];

    token_vector translation_unit;
    token_vector_init(&translation_unit);

    sc_path_table default_paths;
    path_table_init(&default_paths);

    init_preprocessor(&default_paths, mallocator());

    preprocess(file_path, &translation_unit);

    sc_debug("Preprocessor returned translation unit %d tokens long.", translation_unit.size);

    sc_enter_stage("cleaning up");
    token_vector_destroy(&translation_unit);
    release_preprocessor();

    return 0;
}
