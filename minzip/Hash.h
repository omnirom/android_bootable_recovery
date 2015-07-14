/*
 * Copyright 2007 The Android Open Source Project
 *
 * General purpose hash table, used for finding classes, methods, etc.
 *
 * When the number of elements reaches 3/4 of the table's capacity, the
 * table will be resized.
 */
#ifndef _MINZIP_HASH
#define _MINZIP_HASH

#include "inline_magic.h"

#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#ifdef __cplusplus
extern "C" {
#endif

/* compute the hash of an item with a specific type */
typedef unsigned int (*HashCompute)(const void* item);

/*
 * Compare a hash entry with a "loose" item after their hash values match.
 * Returns { <0, 0, >0 } depending on ordering of items (same semantics
 * as strcmp()).
 */
typedef int (*HashCompareFunc)(const void* tableItem, const void* looseItem);

/*
 * This function will be used to free entries in the table.  This can be
 * NULL if no free is required, free(), or a custom function.
 */
typedef void (*HashFreeFunc)(void* ptr);

/*
 * Used by mzHashForeach().
 */
typedef int (*HashForeachFunc)(void* data, void* arg);

/*
 * One entry in the hash table.  "data" values are expected to be (or have
 * the same characteristics as) valid pointers.  In particular, a NULL
 * value for "data" indicates an empty slot, and HASH_TOMBSTONE indicates
 * a no-longer-used slot that must be stepped over during probing.
 *
 * Attempting to add a NULL or tombstone value is an error.
 *
 * When an entry is released, we will call (HashFreeFunc)(entry->data).
 */
typedef struct HashEntry {
    unsigned int hashValue;
    void* data;
} HashEntry;

#define HASH_TOMBSTONE ((void*) 0xcbcacccd)     // invalid ptr value

/*
 * Expandable hash table.
 *
 * This structure should be considered opaque.
 */
typedef struct HashTable {
    int         tableSize;          /* must be power of 2 */
    int         numEntries;         /* current #of "live" entries */
    int         numDeadEntries;     /* current #of tombstone entries */
    HashEntry*  pEntries;           /* array on heap */
    HashFreeFunc freeFunc;
} HashTable;

/*
 * Create and initialize a HashTable structure, using "initialSize" as
 * a basis for the initial capacity of the table.  (The actual initial
 * table size may be adjusted upward.)  If you know exactly how many
 * elements the table will hold, pass the result from mzHashSize() in.)
 *
 * Returns "false" if unable to allocate the table.
 */
HashTable* mzHashTableCreate(size_t initialSize, HashFreeFunc freeFunc);

/*
 * Compute the capacity needed for a table to hold "size" elements.  Use
 * this when you know ahead of time how many elements the table will hold.
 * Pass this value into mzHashTableCreate() to ensure that you can add
 * all elements without needing to reallocate the table.
 */
size_t mzHashSize(size_t size);

/*
 * Clear out a hash table, freeing the contents of any used entries.
 */
void mzHashTableClear(HashTable* pHashTable);

/*
 * Free a hash table.
 */
void mzHashTableFree(HashTable* pHashTable);

/*
 * Get #of entries in hash table.
 */
INLINE int mzHashTableNumEntries(HashTable* pHashTable) {
    return pHashTable->numEntries;
}

/*
 * Get total size of hash table (for memory usage calculations).
 */
INLINE int mzHashTableMemUsage(HashTable* pHashTable) {
    return sizeof(HashTable) + pHashTable->tableSize * sizeof(HashEntry);
}

/*
 * Look up an entry in the table, possibly adding it if it's not there.
 *
 * If "item" is not found, and "doAdd" is false, NULL is returned.
 * Otherwise, a pointer to the found or added item is returned.  (You can
 * tell the difference by seeing if return value == item.)
 *
 * An "add" operation may cause the entire table to be reallocated.
 */
void* mzHashTableLookup(HashTable* pHashTable, unsigned int itemHash, void* item,
    HashCompareFunc cmpFunc, bool doAdd);

/*
 * Remove an item from the hash table, given its "data" pointer.  Does not
 * invoke the "free" function; just detaches it from the table.
 */
bool mzHashTableRemove(HashTable* pHashTable, unsigned int hash, void* item);

/*
 * Execute "func" on every entry in the hash table.
 *
 * If "func" returns a nonzero value, terminate early and return the value.
 */
int mzHashForeach(HashTable* pHashTable, HashForeachFunc func, void* arg);

/*
 * An alternative to mzHashForeach(), using an iterator.
 *
 * Use like this:
 *   HashIter iter;
 *   for (mzHashIterBegin(hashTable, &iter); !mzHashIterDone(&iter);
 *       mzHashIterNext(&iter))
 *   {
 *       MyData* data = (MyData*)mzHashIterData(&iter);
 *   }
 */
typedef struct HashIter {
    void*       data;
    HashTable*  pHashTable;
    int         idx;
} HashIter;
INLINE void mzHashIterNext(HashIter* pIter) {
    int i = pIter->idx +1;
    int lim = pIter->pHashTable->tableSize;
    for ( ; i < lim; i++) {
        void* data = pIter->pHashTable->pEntries[i].data;
        if (data != NULL && data != HASH_TOMBSTONE)
            break;
    }
    pIter->idx = i;
}
INLINE void mzHashIterBegin(HashTable* pHashTable, HashIter* pIter) {
    pIter->pHashTable = pHashTable;
    pIter->idx = -1;
    mzHashIterNext(pIter);
}
INLINE bool mzHashIterDone(HashIter* pIter) {
    return (pIter->idx >= pIter->pHashTable->tableSize);
}
INLINE void* mzHashIterData(HashIter* pIter) {
    assert(pIter->idx >= 0 && pIter->idx < pIter->pHashTable->tableSize);
    return pIter->pHashTable->pEntries[pIter->idx].data;
}


/*
 * Evaluate hash table performance by examining the number of times we
 * have to probe for an entry.
 *
 * The caller should lock the table beforehand.
 */
typedef unsigned int (*HashCalcFunc)(const void* item);
void mzHashTableProbeCount(HashTable* pHashTable, HashCalcFunc calcFunc,
    HashCompareFunc cmpFunc);

#ifdef __cplusplus
}
#endif

#endif /*_MINZIP_HASH*/
