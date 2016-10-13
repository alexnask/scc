#ifndef STRINGS_H__
#define STRINGS_H__

#include <stddef.h>
#include <stdbool.h>

struct NormalString {
    char *data;
    size_t size;
    size_t capacity;
};

// This is a small string optimized string type.
// It uses malloc as a fallback.
// The small string can be up to 23 bytes long.
// Based on FBString.
// The real maximum capacity of a normal string is 2^63 - 1 rather than 2^64 - 1, since a single bit is used as a small string flag.
// If a small string ever gets upgraded to a normal string, it never goes back to beign a small string (unless you destroy + re initialize).
typedef struct string {
    union {
        struct NormalString normal;
        char raw_data[sizeof(struct NormalString)];
    };
} string;

// A string view does not own the memory it points to.
// You can still use it to modify that memory.
// It is essentially just a data and size pair.
typedef struct string_view {
    char *data;
    size_t size;
} string_view;

bool is_small_string(string *str);
char *string_data(string *str);
size_t string_size(string *str);
size_t string_capacity(string *str);

void string_init(string *str, size_t size);
// Equivalent to string_init(str, 0);
void string_init_empty(string *str);
// TODO: pass as pointer in first parameter instead of copying?
// This is already relatively inexpensive (24 byte copy);
void string_from_ptr_size(string *str, const char * const data, size_t size);

// This works with uninitialized strings too.
void set_small_string_size(string *str, size_t new_size);

void string_normal_set_capacity(string *str, size_t new_cap);
void string_resize(string *str, size_t new_size);

void string_append_ptr_size(string *str, const char * const data, size_t size);
void string_append(string *left, string *right);

bool string_equals_ptr_size(string *str, const char * const data, size_t size);
bool string_equals(string *left, string *right);

void string_assign_ptr_size(string *str, const char * const data, size_t size);
void string_assign(string *str, string *other);
void string_copy(string *dest, string *src);

void string_push(string *dest, const char c);

void substring(string *dest, string *src, long int start, long int end);
string_view view(string *src, long int start, long int end);

void string_destroy(string *str);

#define STRING_EQUALS_LITERAL(S, L) string_equals_ptr_size(S, L, sizeof(L) - 1)
#define STRING_FROM_LITERAL(S, L) string_from_ptr_size(S, L, sizeof(L) - 1);
// For use in function calls to expand a string view into a pointer + size argument.
// Note you can use do something like SV2PS(*view) if you have a pointer.
// Will cause double evaluation of SV, meant for use with variables or derefed variables.
#define SV2PS(SV) (SV).data, (SV).size

#endif
