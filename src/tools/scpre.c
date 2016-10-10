// The SCC preprocessor as an executable.
#include <tokenizer.h>
#include <stdio.h>

// int main(int argc, char *argv[]) {
//     // Just assume first argument passed is a file for now.
//     if (argc < 3) {
//         printf("Usage: %s <input file> <output file>\n", argv[0]);
//         return 0;
//     }

//     char *in_path = argv[1];

//     token_vector translation_unit;
//     token_vector_init(&translation_unit, 2 * 1024);

//     sc_path_table default_paths;
//     path_table_init(&default_paths);
//     path_table_add(&default_paths, "include");

//     init_preprocessor(&default_paths, mallocator());

//     preprocess(in_path, &translation_unit);

//     sc_debug("Preprocessor returned translation unit %d tokens long.", translation_unit.size);

//     char *out_path = argv[2];
//     FILE *out_file = fopen(out_path, "w");

//     for (size_t i = 0; i < translation_unit.size; i++) {
//         token *current = &translation_unit.memory[i];
//         fwrite(token_data(current), 1, token_size(current), out_file);
//     }

//     fclose(out_file);

//     sc_enter_stage("cleaning up");
//     token_vector_destroy(&translation_unit);
//     release_preprocessor();

//     return 0;
// }

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <input file>\n", argv[0]);
        return 0;
    }

    char *in_path = argv[1];

    sc_file_cache cache;
    file_cache_init(&cache, mallocator());
    sc_file_cache_handle handle = file_cache_load(&cache, in_path);

    tokenizer_state state;
    tokenizer_state_init(&state, handle);

    pp_token current;
    do {
        next_token(&current, &state);
    } while (current.kind != PP_TOK_EOF);

    return 0;
}
