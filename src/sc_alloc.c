#include <sc_alloc.h>

void *sc_alloc(sc_allocator *alloc, size_t size) {
    return alloc->alloc(alloc->state, size);
}

void sc_free(sc_allocator *alloc, void *memory) {
    alloc->free(alloc->state, memory);
}

void sc_destroy_allocator(sc_allocator *alloc) {
    alloc->destroy(alloc->state);
}

#define UNUSED(x) (void)(x)

static void* malloc_alloc(void *state, size_t size) {
    UNUSED(state);
    return malloc(size);
}

static void malloc_free(void *state, void *memory) {
    UNUSED(state);
    free(memory);
}

static void malloc_destroy(void *state) {
    UNUSED(state);
    assert(!state);
}

sc_allocator *mallocator() {
    static sc_allocator instance = (sc_allocator) { .alloc = malloc_alloc, .free = malloc_free, .destroy = malloc_destroy, .state = NULL };
    return &instance;
}

// The region does not own the memory.
void region_init(sc_region *region, void *memory, size_t size) {
    *region = (sc_region) { .memory = memory, .index = 0, .size = size };
}

void region_destroy(sc_region *region) {}

bool region_can_allocate(sc_region *region, size_t size) {
    return region->index + size <= region->size;
}

bool region_owns(sc_region *region, void *memory) {
    return memory >= region->memory && memory <= region->memory + region->size;
}

void region_clear(sc_region *region) {
    region->index = 0;
}

static void* region_alloc(sc_region *region, size_t size) {
    if (!region_can_allocate(region, size)) {
        return NULL;
    }

    void *ptr = region->memory + region->index;
    region->index += size;
    return ptr;
}

static void region_free(sc_region *region, void *memory) {
    assert(region_owns(region, memory));
    // We can try to rewind the index if the memory is the last thing we allocated, but we would need the size of the allocation to do that.
    // Instead, do nothing.
}

sc_allocator make_region_alloc(sc_region *region, void *memory, size_t size) {
    region_init(region, memory, size);
    return make_alloc_from_region(region);
}

sc_allocator make_alloc_from_region(sc_region *region) {
    return (sc_allocator) { .alloc = (alloc_func)region_alloc, .free = (free_func)region_free, .destroy = (destroy_func)region_destroy, .state = (void*)region };
}

void region_list_init(sc_region_list *list, sc_allocator *backing, size_t region_size) {
    // We always allocate the first node.
    region_init(&list->root.region, sc_alloc(backing, region_size), region_size);
    list->root.next = NULL;
    list->backing_allocator = backing;
    list->region_size = region_size;
}

void region_list_destroy(sc_region_list *list) {
    // We have to erase memory of the regions in reverse order, since each node is held in the previous region.
    // We don't care about the memory we are currently holding, so we are going to use the first region to store the list of regions we have to free.
    sc_region **list_to_free = list->root.region.memory;
    size_t count = 0;

    sc_region_list_node *current = &list->root;
    while (current->next != NULL) {
        list_to_free[count++] = &current->region;
        current = current->next;
    }

    // So now we just have to go in reverse order and free our regions!
    for (size_t i = count - 1; i >= 0; i--) {
        region_destroy(list_to_free[count]);
    }
}

static void* region_list_alloc(sc_region_list *list, size_t size) {
    // Let's find out where we currently are.
    // Meanwhile, we try to allocate in the intermediate nodes where we have already allocated the next node in.
    sc_region_list_node *current = &list->root;
    while (current->next) {
        current = current->next;

        // FIXME: This is ugly, refactor somehow
        if (current->next) {
            // Ok, we've already allocated the next node.
            // Do we fit here?
            if (region_can_allocate(&current->next->region, size)) {
                return region_alloc(&current->next->region, size);
            }
        }
    }

    // Right.
    // So we either need a new region or can fit in the last one in the list.
    // Note that we are checking against the size of the allocation + the size of a new node.
    if (region_can_allocate(&current->region, size + sizeof(sc_region_list_node))) {
        // Ok, let's go ahead and allocate.
        return region_alloc(&current->region, size);
    } else {
        // Ok, let's allocate our next node, allocate its region and go on.
        assert(region_can_allocate(&current->region, sizeof(sc_region_list_node)));

        sc_region_list_node *new_node = region_alloc(&current->region, sizeof(sc_region_list_node));
        region_init(&new_node->region, sc_alloc(list->backing_allocator, list->region_size), list->region_size);
        new_node->next = NULL;

        current->next = new_node;
        // size would have to be huge for this to fail.
        assert(region_can_allocate(&new_node->region, size + sizeof(sc_region_list_node)));
        return region_alloc(&new_node->region, size);
    }
}

// Nothing we can do.
static void region_list_free(void *state, void *memory) {
    UNUSED(state);
    UNUSED(memory);
}

sc_allocator make_region_list_alloc(sc_region_list *list, sc_allocator *backing, size_t region_size) {
    region_list_init(list, backing, region_size);

    return make_alloc_from_region_list(list);
}

sc_allocator make_alloc_from_region_list(sc_region_list *list) {
    return (sc_allocator) { .alloc = (alloc_func)region_list_alloc, .free = region_list_free, .destroy = (destroy_func)region_list_destroy, .state = (void*)list };
}

void fallback_init(sc_fallback *alloc, sc_allocator *primary, sc_allocator *fallback) {
    alloc->primary = primary;
    alloc->fallback = fallback;
}

void fallback_destroy(sc_fallback *alloc) {
    UNUSED(alloc);
}

static void *fallback_alloc(sc_fallback *fb, size_t size) {
    // We allocate an extra flag byte.
    // 0 is primary allocator, anything else is fallback.
    char *candidate = sc_alloc(fb->primary, size + 1);

    if (!candidate) {
        candidate = sc_alloc(fb->fallback, size + 1);

        if (!candidate) {
            return NULL;
        }

        candidate[0] = 1;
    }
    else {
        candidate[0] = 0;
    }


    return candidate + 1;
}

static void fallback_free(sc_fallback *fb, char *memory) {
    if (memory[-1] == 0) {
        sc_free(fb->primary, memory - 1);
    }
    else {
        sc_free(fb->fallback, memory - 1);
    }
}

sc_allocator make_fallback_alloc(sc_fallback *fb, sc_allocator *primary, sc_allocator *fallback) {
    fallback_init(fb, primary, fallback);

    return make_alloc_from_fallback(fb);
}

sc_allocator make_alloc_from_fallback(sc_fallback *fb) {
    return (sc_allocator) { .alloc = (alloc_func)fallback_alloc, .free = (free_func)fallback_free, .destroy = (destroy_func)fallback_destroy, .state = (void*)fb };
}

#undef UNUSED
