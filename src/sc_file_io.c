#include <sc_file_io.h>
#include <string.h>

#ifdef _WIN32
    const char separator = '\\';
#else
    const char separator = '/';
#endif

// Combines an absolute and a relative path
// Returns bytes written to 'out' (including null terminator if present)
size_t path_abs_rel_combine(const char *abs_path, const char *rel_path, size_t rel_len, char *out, size_t out_max_len) {
    size_t abs_len = strlen(abs_path);
    size_t write_size = out_max_len < abs_len ? out_max_len : abs_len;
    size_t written = write_size;
    // We do not use the _s (safe) version since we are doing the checking ourselves
    strncpy(out, abs_path, write_size);

    // We wrote as much as we can, get out.
    if (written == out_max_len) {
        return written;
    }

    if (abs_path[abs_len - 1] != separator) {
        // Add separator
        out[abs_len] = separator;
        written++;
    }

    // Skip past the absolute path.
    out += written;

    // Maximum write size.
    write_size = out_max_len - written;

    // We can fit the relative length + null terminator
    if (rel_len + 1 < write_size) {
        write_size = rel_len + 1;
    }

    strncpy(out, rel_path, write_size - 1);
    out[write_size] = '\0';
    written += write_size;
    return written;
}

void path_table_init(sc_path_table *table) {
    table->capacity = PATH_TABLE_BLOCK_SIZE;
    table->size = 0;
    table->memory = malloc(PATH_TABLE_BLOCK_SIZE * sizeof(char *));
}

void path_table_add(sc_path_table *table, const char *path) {
    if (table->size >= table->capacity) {
        // We need to reallocate.
        table->capacity += PATH_TABLE_BLOCK_SIZE;
        table->memory = realloc(table->memory, table->capacity * sizeof(char *));
    }

    table->memory[table->size++] = path;
}

void path_table_destroy(sc_path_table *table) {
    table->capacity = table->size = 0;
    free(table->memory);
    table->memory = NULL;
}

bool path_table_lookup(sc_path_table *table, const char *relative_path, char *absolute_path, size_t absolute_max_len) {
    if (table->size == 0) {
        return false;
    }

    size_t rel_len = strlen(relative_path);

    for (size_t i = 0; i < table->size; ++i) {
        const char *current_path = table->memory[i];

        // size_t path_abs_rel_combine(const char *abs_path, size_t abs_len, const char *rel_path, size_t rel_len, char *out, size_t out_max_len)
        char combined_path[FILENAME_MAX];
        size_t combined_len = path_abs_rel_combine(current_path, relative_path, rel_len, combined_path, FILENAME_MAX);

        assert(combined_len <= FILENAME_MAX);
        assert(combined_path[combined_len] == '\0');

        // Check if that file exists
        FILE *fhandle = fopen(combined_path, "r");
        if (fhandle) {
            // File exists, we found it guys!
            fclose(fhandle);

            size_t copied_chars = combined_len > absolute_max_len ? absolute_max_len : combined_len;
            strncpy(absolute_path, combined_path, copied_chars);
            return true;
        }
    }

    return false;
}

void file_load(sc_file *file, char *abs_path, sc_allocator *alloc) {
    FILE *stream =  fopen(abs_path, "r");

    if (!stream) {
        file->contents = NULL;
        file->size = 0L;
        file->alloc = NULL;
        file->abs_path = NULL;
        return;
    }

    file->abs_path = abs_path;

    fseek(stream, 0L, SEEK_END);
    file->size = ftell(stream);
    rewind(stream);

    file->alloc = alloc;
    file->contents = sc_alloc(alloc, file->size + 1);

    fread(file->contents, 1, file->size, stream);
    file->contents[file->size] = '\0';

    fclose(stream);
}

void file_destroy(sc_file *file) {
    sc_free(file->alloc, file->contents);
    file->size = 0L;
    file->alloc = NULL;
    file->contents = NULL;
    file->abs_path = NULL;
}

void file_cache_init(sc_file_cache *cache, sc_allocator *alloc) {
    cache->alloc = alloc;
    cache->size = 0;
    cache->capacity = FILE_CACHE_BLOCK_SIZE;

    cache->files = malloc(FILE_CACHE_BLOCK_SIZE * sizeof(sc_file));
}

sc_file_cache_handle file_cache_load(sc_file_cache *cache, const char *abs_path) {
    for (size_t i = 0; i < cache->size; ++i) {
        // Look up wether we already own this file.
        if (!strcmp(cache->files[i].abs_path, abs_path)) {
            // Already have it!
            return (sc_file_cache_handle) { .cache = cache, .index = i };
        }
    }

    // Ok, we need to add the file.
    if (cache->size >= cache->capacity) {
        // Need to reallocate.
        cache->capacity += FILE_CACHE_BLOCK_SIZE;
        cache->files = realloc(cache->files, cache->capacity * sizeof(sc_file));
    }

    // We will use our allocator to keep the absolute path.
    size_t path_len = strlen(abs_path);
    char *new_abs_path = sc_alloc(cache->alloc, path_len + 1);
    strncpy(new_abs_path, abs_path, path_len);
    new_abs_path[path_len] = '\0';

    file_load(&cache->files[cache->size++], new_abs_path, cache->alloc);
    if (!cache->files[cache->size - 1].contents) {
        cache->size--;
        // File does not exist.
        return (sc_file_cache_handle) { .cache = NULL, .index = 0 };
    }

    return (sc_file_cache_handle) { .cache = cache, .index = cache->size - 1 };
}

// This simply unloads the file memory (destroys the sc_file), doesn't rearange things.
void file_cache_unload(sc_file_cache *cache, const char *abs_path) {
    for (size_t i = 0; i < cache->size; ++i) {
        // Look up wether we already own this file.
        if (!strcmp(cache->files[i].abs_path, abs_path)) {
            // Already have it!
            sc_free(cache->alloc, cache->files[i].abs_path);
            file_destroy(&cache->files[i]);
        }
    }
}

void file_cache_destroy(sc_file_cache *cache) {
    // Destroy all our files
    for (size_t i = 0; i < cache->size; ++i) {
        sc_free(cache->alloc, cache->files[i].abs_path);
        file_destroy(&cache->files[i]);
    }

    cache->size = 0;
    cache->capacity = 0;
    free(cache->files);
    cache->files = NULL;
    cache->alloc = NULL;
}

sc_file *handle_to_file(sc_file_cache_handle handle) {
    return &handle.cache->files[handle.index];
}

// TODO: WE NEED SEPARATOR CONVERSION TO '/' (for win32)

void get_relative_path_from_file(const char *absolute_path, const char *relative_path, char *out, size_t out_max_len) {
    // Right
    // Let's copy up to our last directory separator.
    // (actually, let's find it first :P)
    size_t abs_len = strlen(absolute_path), rel_len = strlen(relative_path);
    size_t last_sep_index = 0;
    for (size_t i = 0; i < abs_len; i++) {
        if (absolute_path[i] == separator) {
            last_sep_index = i;
        }
    }

    // Copy up to there (including that character).
    // "abc/" -> lsi = 3
    size_t written = last_sep_index + 1 < out_max_len ? last_sep_index + 1 : out_max_len;
    strncpy(out, absolute_path, written);

    if (written == out_max_len) return;

    out += written;
    written = rel_len + 1 > (out_max_len - written) ? out_max_len - written : rel_len + 1;
    strncpy(out, relative_path, written);
}
