// The SCC preprocessor as an executable.
#include <tokenizer.h>

#include <stdio.h>

int main(int argc, char *argv[]) {
    // Just assume first argument passed is a file for now.
    if (argc < 2) {
        printf("No file supplied to scpre.\n");
        printf("Usage: %s <file>\n", argv[0]);
        return 0;
    }

    char *file_path = argv[1];

    tokenizer_state state;
    sc_file_cache file_cache;

    file_cache_init(&file_cache, mallocator());
    sc_file_cache_handle handle = file_cache_load(&file_cache, file_path);
    if (!handle.cache) {
        sc_error(true, "Could not load file %s.\nAre you sure it exists?", file_path);
    }

    tokenizer_state_init(&state, handle);

    sc_enter_stage("tokenizing");

    token current;
    do {
        next_token(&current, &state);
    } while (current.kind != TOK_EOF);

    file_cache_destroy(&file_cache);

    return 0;
}
