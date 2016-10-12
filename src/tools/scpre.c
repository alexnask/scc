// The SCC preprocessor as an executable.
#include <token_vector.h>
#include <stdio.h>

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

    pp_token_vector line_vec;
    pp_token_vector_init(&line_vec, 128);

    while(tokenize_line(&line_vec, &state)) {
        for (size_t i = 0; i < line_vec.size; i++) {
            if (line_vec.memory[i].kind != PP_TOK_WHITESPACE) {
                printf("%d: %s\n", line_vec.memory[i].kind, string_data(&line_vec.memory[i].data));
            }
        }
        line_vec.size = 0;
    }

    return 0;
}
