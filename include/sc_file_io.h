#ifndef SC_FILE_IO_H__
#define SC_FILE_IO_H__

#include <stdio.h>
#include <sc_alloc.h>

#ifndef PATH_TABLE_BLOCK_SIZE
    #define PATH_TABLE_BLOCK_SIZE 16
#endif

#ifndef FILE_CACHE_BLOCK_SIZE
    #define FILE_CACHE_BLOCK_SIZE 16
#endif

// Combines an absolute and a relative path
// Returns bytes written to 'out' (including null terminator if present)
size_t path_abs_rel_combine(const char *abs_path, const char *rel_path, size_t rel_len, char *out, size_t out_max_len);

// We sue malloc/realloc/free (we could use sc_allocator if we add realloc capabilities)
typedef struct sc_path_table {
    const char **memory;
    size_t capacity;
    size_t size;
} sc_path_table;

void path_table_init(sc_path_table *table);
// Caller needs to take care of path's memory (does not use allocator to copy over)
void path_table_add(sc_path_table *table, const char *path);
void path_table_destroy(sc_path_table *table);

// Looks for a file from a relative path in all of the path table
// On success (found a file), returns true and writes to 'absolute_path' for up to 'absolute_max_len' bytes.
// On failure, returns false and does not write to 'absolute_path'
// TODO: Could return number of bytes written instead?
bool path_table_lookup(sc_path_table *table, const char *relative_path, char *absolute_path, size_t absolute_max_len);

typedef struct sc_file {
    char *contents;
    long int size;
    sc_allocator *alloc;

    const char *abs_path;
} sc_file;

// Note that abs_path will be stored in the sc_file.
// If the file does not exist, returns a zero-filled sc_file. (contents = size = alloc = abs_path = 0)
void file_load(sc_file *file, const char *abs_path, sc_allocator *alloc);
// This will __NOT__ destroy the abs_path.
void file_destroy(sc_file *file);

// Caches by absolute path.
// 'alloc' is used for the file contents.
typedef struct sc_file_cache {
    sc_file *files;
    size_t capacity;
    size_t size;

    sc_allocator *alloc;
} sc_file_cache;

typedef struct sc_file_cache_handle {
    sc_file_cache *cache;
    size_t index;
} sc_file_cache_handle;

void file_cache_init(sc_file_cache *cache, sc_allocator *alloc);
sc_file_cache_handle file_cache_load(sc_file_cache *cache, const char *abs_path);
void file_cache_unload(sc_file_cache *cache, const char *abs_path);
void file_cache_destroy(sc_file_cache *cache);

sc_file *handle_to_file(sc_file_cache_handle handle);

void get_relative_path_from_file(const char *absolute_path, const char *relative_path, char *out, size_t out_max_len);

#endif
