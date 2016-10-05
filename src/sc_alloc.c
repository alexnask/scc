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

void region_init(sc_region *region, sc_allocator *backing) {
    *region = (sc_region) { .memory = sc_alloc(backing, REGION_SIZE), .backing_allocator = backing, .index = 0 };
}

void region_destroy(sc_region *region) {
    sc_free(region->backing_allocator, region->memory);
}

bool region_can_allocate(sc_region *region, size_t size) {
    return region->index + size <= REGION_SIZE;
}

bool region_owns(sc_region *region, void *memory) {
    return memory >= region->memory && memory <= region->memory + REGION_SIZE;
}

void region_clear(sc_region *region) {
    region->index = 0;
}

static void* region_alloc(sc_region *region, size_t size) {
    assert(region_can_allocate(region, size));

    void *ptr = region->memory + region->index;
    region->index += size;
    return ptr;
}

static void region_free(sc_region *region, void *memory) {
    assert(region_owns(region, memory));
    // We can try to rewind the index if the memory is the last thing we allocated, but we would need the size of the allocation to do that.
    // Instead, do nothing.
}

sc_allocator make_region_alloc(sc_region *region, sc_allocator *backing) {
    region_init(region, backing);
    return make_alloc_from_region(region);
}

sc_allocator make_alloc_from_region(sc_region *region) {
    return (sc_allocator) { .alloc = (alloc_func)region_alloc, .free = (free_func)region_free, .destroy = (destroy_func)region_destroy, .state = (void*)region };
}

void region_list_init(sc_region_list *list, sc_allocator *backing) {
    // We always allocate the first node.
    region_init(&list->root.region, backing);
    list->root.next = NULL;
    list->backing_allocator = backing;
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
        region_init(&new_node->region, list->backing_allocator);
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

sc_allocator make_region_list_alloc(sc_region_list *list, sc_allocator *backing) {
    region_list_init(list, backing);

    return make_alloc_from_region_list(list);
}

sc_allocator make_alloc_from_region_list(sc_region_list *list) {
    return (sc_allocator) { .alloc = (alloc_func)region_list_alloc, .free = region_list_free, .destroy = (destroy_func)region_list_destroy, .state = (void*)list };
}

#undef UNUSED
