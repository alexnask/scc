#include <strings.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

// TODO: This is little endian only.
#define CATEGORY_MASK 0x80
#define CATEGORY_SHIFT ((sizeof(size_t) - 1) * 8)
#define LAST_CHAR (sizeof(string) - 1)

// Upgrades a small string to a normal string with initial size.
static void _string_upgrade(string *str, size_t initial_size) {
    assert(is_small_string(str));

    str->normal.size = initial_size;
    string_normal_set_capacity(str, initial_size);
    str->normal.data = malloc(initial_size + 1);
    str->normal.data[initial_size] = '\0';
}

bool is_small_string(string *str) {
    // So, we need to check the flags in capacity.
    // We just apply our category mask to the last byte and return if we are 0.
    return (str->raw_data[LAST_CHAR] & CATEGORY_MASK) == 0;
}

size_t string_size(string *str) {
    return is_small_string(str) ? (LAST_CHAR - str->raw_data[LAST_CHAR]) : str->normal.size;
}

size_t string_capacity(string *str) {
    if (is_small_string(str)) {
        return LAST_CHAR;
    }

    return str->normal.capacity & (~((size_t)(CATEGORY_MASK) << CATEGORY_SHIFT));
}

char *string_data(string *str) {
    return is_small_string(str) ? str->raw_data : str->normal.data;
}

void set_small_string_size(string *str, size_t new_size) {
    assert(is_small_string(str));
    assert(new_size <= LAST_CHAR);

    str->raw_data[LAST_CHAR] = LAST_CHAR - new_size;
    str->raw_data[new_size] = '\0';
}

void string_normal_set_capacity(string *str, size_t new_cap) {
    assert(!is_small_string(str));

    // This cast + shift + or keeps the first bit of the last capacity byte set.
    str->normal.capacity = new_cap | ((size_t)(CATEGORY_MASK) << CATEGORY_SHIFT);
}

void string_init(string *str, size_t size) {
    if (size <= LAST_CHAR) {
        // We can use a small string!
        set_small_string_size(str, size);
    } else {
        // Ok, we need to allocate.
        _string_upgrade(str, size);
    }
}

void string_init_empty(string *str) {
    // Let's be smart, we know it's going to be a small string.
    set_small_string_size(str, 0);
}

void string_from_ptr_size(string *str, const char * const data, size_t size) {
    string_init(str, size);
    memcpy(string_data(str), data, size);
}

void string_resize(string *str, size_t new_size) {
    if (is_small_string(str)) {
        // Ok, we are a small string, we may need to be upgraded to a normal string.
        if (new_size <= LAST_CHAR) {
            // Ok!
            set_small_string_size(str, new_size);
        } else {
            // Let's upgrade!
            _string_upgrade(str, new_size);
        }
    } else {
        // Note that we don't revert back to a small string if our size becomes managable.
        // Could be added.
        // We only need to reallocate if the new size is greater than our old capacity.
        if (string_capacity(str) < new_size) {
            string_normal_set_capacity(str, new_size);
            str->normal.data = realloc(str->normal.data, new_size + 1);
        }
        str->normal.size = new_size;
        str->normal.data[new_size] = '\0';
    }
}

void string_append_ptr_size(string *str, const char * const data, size_t size) {
    size_t old_size = string_size(str);
    if (old_size + size > string_capacity(str)) {
        // We need to resize to fit.
        string_resize(str, old_size + size);
    }

    // Copy our data over.
    memcpy(string_data(str) + old_size, data, size);
}

void string_append(string *left, string *right) {
    string_append_ptr_size(left, string_data(right), string_size(right));
}

bool string_equals_ptr_size(string *str, const char * const data, size_t size) {
    if (string_size(str) != size) return false;

    return !strncmp(string_data(str), data, size);
}

bool string_equals(string *left, string *right) {
    return string_equals_ptr_size(left, string_data(right), string_size(right));
}

void string_assign_ptr_size(string *str, const char * const data, size_t size) {
    // Ok, let's check what kind of string we are
    if (is_small_string(str)) {
        // If we still fit in a small string, just copy data over.
        if (size <= LAST_CHAR) {
            set_small_string_size(str, size);
            memcpy(str->raw_data, data, size);
        } else {
            // We need to upgrade + copy
            _string_upgrade(str, size);
            memcpy(str->normal.data, data, size);
        }
    } else {
        // Ok, we just need to resize and copy over data.
        string_resize(str, size);
        memcpy(str->normal.data, data, size);
    }
}

void string_assign(string *str, string *other) {
    // Ok, let's check what kind of string we are
    if (is_small_string(str)) {
        // If the other guy fits in a small string, we will just copy his data into ours.
        // (if he is a small string, this is just a simple assign but he could be a normal string with a small size).
        if (is_small_string(other)) {
            *str = *other;
        } else {
            size_t length = string_size(other);
            if (length <= LAST_CHAR) {
                set_small_string_size(str, length);
                memcpy(str->raw_data, other->normal.data, length);
            } else {
                // We need to get upgraded to a normal string.
                _string_upgrade(str, length);
                memcpy(str->normal.data, other->normal.data, length);
            }
        }
    } else {
        // Ok, we just need to know the data and length of the other guy.
        size_t length = string_size(other);
        char *data = string_data(other);

        // We need to resize.
        string_resize(str, length);
        // Then copy over data.
        memcpy(str->normal.data, data, length);
    }
}

void string_copy(string *dest, string *src) {
    if (is_small_string(src)) {
        *dest = *src;
    } else {
        string_from_ptr_size(dest, string_data(src), string_size(src));
    }
}

void string_push(string *dest, const char c) {
    string_append_ptr_size(dest, &c, 1);
}

void substring(string *dest, string *src, long int start, long int end) {
    size_t source_length = string_size(src);
    // We support negative indices.
    start = start < 0 ? source_length + start : start;
    end = end < 0 ? source_length + end : end;

    assert(start < source_length);
    assert(end <= source_length);
    assert(start >= 0 && end >= 0);
    assert(end - start >= 0);

    size_t dest_length = end - start;
    string_init(dest, dest_length);

    string_assign_ptr_size(dest, string_data(src) + start, dest_length);
}

string_view view(string *src, long int start, long int end) {
    size_t source_length = string_size(src);
    // We support negative indices.
    start = start < 0 ? source_length + start : start;
    end = end < 0 ? source_length + end : end;

    assert(start < source_length);
    assert(end <= source_length);
    assert(start >= 0 && end >= 0);
    assert(end - start >= 0);

    size_t dest_length = end - start;
    return (string_view) { .data = string_data(src), .size = dest_length };
}

void string_destroy(string *str) {
    if (!is_small_string(str)) {
        // Nothing to do for small strings, free() our memory for normal strings.
        // We could resize to 0 but we assume this isn't going to be reused.
        free(str->normal.data);
        str->normal.data = NULL;
    }
}

#undef LAST_CHAR
#undef CATEGORY_MASK
#undef CATEGORY_SHIFT
