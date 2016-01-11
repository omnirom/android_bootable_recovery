#include "dynarray.h"
#include <stdlib.h>
#include <limits.h>
#include <string.h>

void
dynarray_init( dynarray_t *a )
{
    a->count = a->capacity = 0;
    a->items = NULL;
}


static void
dynarray_reserve_more( dynarray_t *a, int count )
{
    int old_cap = a->capacity;
    int new_cap = old_cap;
    const int max_cap = INT_MAX/sizeof(void*);
    void** new_items;
    int new_count = a->count + count;

    if (count <= 0)
        return;

    if (count > max_cap - a->count)
        abort();

    new_count = a->count + count;

    while (new_cap < new_count) {
        old_cap = new_cap;
        new_cap += (new_cap >> 2) + 4;
        if (new_cap < old_cap || new_cap > max_cap) {
            new_cap = max_cap;
        }
    }
    new_items = realloc(a->items, new_cap*sizeof(void*));
    if (new_items == NULL)
        abort();

    a->items = new_items;
    a->capacity = new_cap;
}

void
dynarray_append( dynarray_t *a, void* item )
{
    if (a->count >= a->capacity)
        dynarray_reserve_more(a, 1);

    a->items[a->count++] = item;
}

void
dynarray_done( dynarray_t *a )
{
    free(a->items);
    a->items = NULL;
    a->count = a->capacity = 0;
}

// string arrays

void strlist_init( strlist_t *list )
{
    dynarray_init(list);
}

void strlist_append_b( strlist_t *list, const void* str, size_t  slen )
{
    char *copy = malloc(slen+1);
    memcpy(copy, str, slen);
    copy[slen] = '\0';
    dynarray_append(list, copy);
}

void strlist_append_dup( strlist_t *list, const char *str)
{
    strlist_append_b(list, str, strlen(str));
}

void strlist_done( strlist_t *list )
{
    STRLIST_FOREACH(list, string, free(string));
    dynarray_done(list);
}

static int strlist_compare_strings(const void* a, const void* b)
{
    const char *sa = *(const char **)a;
    const char *sb = *(const char **)b;
    return strcmp(sa, sb);
}

void strlist_sort( strlist_t *list )
{
    if (list->count > 0) {
        qsort(list->items,
              (size_t)list->count,
              sizeof(void*),
              strlist_compare_strings);
    }
}
