// The SCC preprocessor as an executable.
#include <preprocessor.h>
#include <stdio.h>

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s <input file> <output file>\n", argv[0]);
        return 0;
    }

    char *in_path = argv[1];
    char *out_path = argv[2];

    sc_file_cache cache;
    file_cache_init(&cache, mallocator());
    sc_file_cache_handle handle = file_cache_load(&cache, in_path);

    tokenizer_state state;
    tokenizer_state_init(&state, handle);

    pp_token_vector line_vec;
    pp_token_vector_init(&line_vec, 128);

    // We'll go line by line to append newlines for a more human readable form.
    token_vector translation_line;
    token_vector_init(&translation_line, 128);

    preprocessor_state pp_state;
    preprocessor_state_init(&pp_state, &state, &translation_line, &line_vec);

    FILE *out = fopen(out_path, "w");

    bool ok = true;
    while (ok) {
        ok = preprocess_line(&pp_state);
        for (size_t i = 0; i < translation_line.size; i++) {
            fwrite(string_data(&translation_line.memory[i].data), 1, string_size(&translation_line.memory[i].data), out);
            if (i < translation_line.size - 1 && translation_line.memory[i].has_whitespace) {
                putc(' ', out);
            }
        }
        if (translation_line.size != 0) {
            // Write a newline!
            putc('\n', out);
        }

        translation_line.size = 0;
    }

    fclose(out);

    return 0;
}
