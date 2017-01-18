#ifndef DYNARRAY_H
#define DYNARRAY_H

#include <stddef.h>

/* simple dynamic array of pointers */
typedef struct {
    int count;
    int capacity;
    void** items;
} dynarray_t;

#define DYNARRAY_INITIALIZER  { 0, 0, NULL }

void dynarray_init( dynarray_t *a );
void dynarray_done( dynarray_t *a );

void dynarray_append( dynarray_t *a, void* item );

/* Used to iterate over a dynarray_t
 * _array :: pointer to the array
 * _item_type :: type of objects pointed to by the array
 * _item      :: name of a local variable defined within the loop
 *               with type '_item_type'
 * _stmnt     :: C statement that will be executed in each iteration.
 *
 * You case use 'break' and 'continue' within _stmnt
 *
 * This macro is only intended for simple uses. I.e. do not add or
 * remove items from the array during iteration.
 */
#define DYNARRAY_FOREACH_TYPE(_array,_item_type,_item,_stmnt) \
    do { \
        int _nn_##__LINE__ = 0; \
        for (;_nn_##__LINE__ < (_array)->count; ++ _nn_##__LINE__) { \
            _item_type _item = (_item_type)(_array)->items[_nn_##__LINE__]; \
            _stmnt; \
        } \
    } while (0)

#define DYNARRAY_FOREACH(_array,_item,_stmnt) \
    DYNARRAY_FOREACH_TYPE(_array,void *,_item,_stmnt)

/* Simple dynamic string arrays
 *
 * NOTE: A strlist_t owns the strings it references.
 */
typedef dynarray_t  strlist_t;

#define  STRLIST_INITIALIZER  DYNARRAY_INITIALIZER

/* Used to iterate over a strlist_t
 * _list   :: pointer to strlist_t object
 * _string :: name of local variable name defined within the loop with
 *            type 'char*'
 * _stmnt  :: C statement executed in each iteration
 *
 * This macro is only intended for simple uses. Do not add or remove items
 * to/from the list during iteration.
 */
#define  STRLIST_FOREACH(_list,_string,_stmnt) \
    DYNARRAY_FOREACH_TYPE(_list,char *,_string,_stmnt)

void strlist_init( strlist_t *list );

/* note: strlist_done will free all the strings owned by the list */
void strlist_done( strlist_t *list );

/* append a new string made of the first 'slen' characters from 'str'
 * followed by a trailing zero.
 */
void strlist_append_b( strlist_t *list, const void* str, size_t  slen );

/* append the copy of a given input string to a strlist_t */
void strlist_append_dup( strlist_t *list, const char *str);

/* sort the strings in a given list (using strcmp) */
void strlist_sort( strlist_t *list );

#endif /* DYNARRAY_H */