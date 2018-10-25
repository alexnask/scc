#ifndef SC_ALLOC_H__
#define SC_ALLOC_H__

#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

typedef void* (*alloc_func)(void*, size_t);
typedef void (*free_func)(void*, void*);
typedef void (*destroy_func)(void*);

// Allocator interface
typedef struct sc_allocator {
    alloc_func alloc;
    free_func free;
    destroy_func destroy;
    void *state;
} sc_allocator;

void *sc_alloc(sc_allocator *alloc, size_t size);
void sc_free(sc_allocator *alloc, void *memory);
void sc_destroy_allocator(sc_allocator *alloc);

// Get the mallocator.
sc_allocator *mallocator();

// Region interface
// The region does not own the memory it manages.
typedef struct sc_region {
    void *memory;
    size_t index;
    size_t size;
} sc_region;

void init_region(sc_region *region, void *memory, size_t size);
void destroy_region(sc_region *region);

bool region_can_allocate(sc_region *region, size_t size);
bool region_owns(sc_region *region, void *memory);
void region_clear(sc_region *region);

// init + make_alloc_from
sc_allocator make_region_alloc(sc_region *region, void *memory, size_t size);
sc_allocator make_alloc_from_region(sc_region *region);

typedef struct sc_region_list_node {
    sc_region region;
    struct sc_region_list_node *next;
} sc_region_list_node;

// Each node is stored in the previous region, so you only ever call into the backing allocator for the region memory.
// You should destroy the region list after a substantial amount of objects have been "freed", since it will just keep on requesting more and more memory.
// The goal is to allocate substantial chunks of memory to improve cache locality in certain parts of your application, not to be used as a general purpose allocator.
typedef struct sc_region_list {
    sc_region_list_node root;
    sc_allocator *backing_allocator;
    size_t region_size;
} sc_region_list;

void region_list_init(sc_region_list *list, sc_allocator *backing, size_t region_size);
void region_list_destroy(sc_region_list *list);

sc_allocator make_region_list_alloc(sc_region_list *list, sc_allocator *backing, size_t region_size);
sc_allocator make_alloc_from_region_list(sc_region_list *list);

typedef struct sc_fallback {
    sc_allocator *primary;
    sc_allocator *fallback;
} sc_fallback;

void fallback_init(sc_fallback *alloc, sc_allocator *primary, sc_allocator *fallback);
void fallback_destroy(sc_fallback *alloc);

sc_allocator make_fallback_alloc(sc_fallback *fb, sc_allocator *primary, sc_allocator *fallback);
sc_allocator make_alloc_from_fallback(sc_fallback *fb);

#endif
