/*
 * Copyright 2006 The Android Open Source Project
 *
 * Hash table.  The dominant calls are add and lookup, with removals
 * happening very infrequently.  We use probing, and don't worry much
 * about tombstone removal.
 */
#include <stdlib.h>
#include <assert.h>

#define LOG_TAG "minzip"
#include "Log.h"
#include "Hash.h"

/* table load factor, i.e. how full can it get before we resize */
//#define LOAD_NUMER  3       // 75%
//#define LOAD_DENOM  4
#define LOAD_NUMER  5       // 62.5%
#define LOAD_DENOM  8
//#define LOAD_NUMER  1       // 50%
//#define LOAD_DENOM  2

/*
 * Compute the capacity needed for a table to hold "size" elements.
 */
size_t mzHashSize(size_t size) {
    return (size * LOAD_DENOM) / LOAD_NUMER +1;
}

/*
 * Round up to the next highest power of 2.
 *
 * Found on http://graphics.stanford.edu/~seander/bithacks.html.
 */
unsigned int roundUpPower2(unsigned int val)
{
    val--;
    val |= val >> 1;
    val |= val >> 2;
    val |= val >> 4;
    val |= val >> 8;
    val |= val >> 16;
    val++;

    return val;
}

/*
 * Create and initialize a hash table.
 */
HashTable* mzHashTableCreate(size_t initialSize, HashFreeFunc freeFunc)
{
    HashTable* pHashTable;

    assert(initialSize > 0);

    pHashTable = (HashTable*) malloc(sizeof(*pHashTable));
    if (pHashTable == NULL)
        return NULL;

    pHashTable->tableSize = roundUpPower2(initialSize);
    pHashTable->numEntries = pHashTable->numDeadEntries = 0;
    pHashTable->freeFunc = freeFunc;
    pHashTable->pEntries =
        (HashEntry*) calloc((size_t)pHashTable->tableSize, sizeof(HashTable));
    if (pHashTable->pEntries == NULL) {
        free(pHashTable);
        return NULL;
    }

    return pHashTable;
}

/*
 * Clear out all entries.
 */
void mzHashTableClear(HashTable* pHashTable)
{
    HashEntry* pEnt;
    int i;

    pEnt = pHashTable->pEntries;
    for (i = 0; i < pHashTable->tableSize; i++, pEnt++) {
        if (pEnt->data == HASH_TOMBSTONE) {
            // nuke entry
            pEnt->data = NULL;
        } else if (pEnt->data != NULL) {
            // call free func then nuke entry
            if (pHashTable->freeFunc != NULL)
                (*pHashTable->freeFunc)(pEnt->data);
            pEnt->data = NULL;
        }
    }

    pHashTable->numEntries = 0;
    pHashTable->numDeadEntries = 0;
}

/*
 * Free the table.
 */
void mzHashTableFree(HashTable* pHashTable)
{
    if (pHashTable == NULL)
        return;
    mzHashTableClear(pHashTable);
    free(pHashTable->pEntries);
    free(pHashTable);
}

#ifndef NDEBUG
/*
 * Count up the number of tombstone entries in the hash table.
 */
static int countTombStones(HashTable* pHashTable)
{
    int i, count;

    for (count = i = 0; i < pHashTable->tableSize; i++) {
        if (pHashTable->pEntries[i].data == HASH_TOMBSTONE)
            count++;
    }
    return count;
}
#endif

/*
 * Resize a hash table.  We do this when adding an entry increased the
 * size of the table beyond its comfy limit.
 *
 * This essentially requires re-inserting all elements into the new storage.
 *
 * If multiple threads can access the hash table, the table's lock should
 * have been grabbed before issuing the "lookup+add" call that led to the
 * resize, so we don't have a synchronization problem here.
 */
static bool resizeHash(HashTable* pHashTable, int newSize)
{
    HashEntry* pNewEntries;
    int i;

    assert(countTombStones(pHashTable) == pHashTable->numDeadEntries);

    pNewEntries = (HashEntry*) calloc(newSize, sizeof(HashTable));
    if (pNewEntries == NULL)
        return false;

    for (i = 0; i < pHashTable->tableSize; i++) {
        void* data = pHashTable->pEntries[i].data;
        if (data != NULL && data != HASH_TOMBSTONE) {
            int hashValue = pHashTable->pEntries[i].hashValue;
            int newIdx;

            /* probe for new spot, wrapping around */
            newIdx = hashValue & (newSize-1);
            while (pNewEntries[newIdx].data != NULL)
                newIdx = (newIdx + 1) & (newSize-1);

            pNewEntries[newIdx].hashValue = hashValue;
            pNewEntries[newIdx].data = data;
        }
    }

    free(pHashTable->pEntries);
    pHashTable->pEntries = pNewEntries;
    pHashTable->tableSize = newSize;
    pHashTable->numDeadEntries = 0;

    assert(countTombStones(pHashTable) == 0);
    return true;
}

/*
 * Look up an entry.
 *
 * We probe on collisions, wrapping around the table.
 */
void* mzHashTableLookup(HashTable* pHashTable, unsigned int itemHash, void* item,
    HashCompareFunc cmpFunc, bool doAdd)
{
    HashEntry* pEntry;
    HashEntry* pEnd;
    void* result = NULL;

    assert(pHashTable->tableSize > 0);
    assert(item != HASH_TOMBSTONE);
    assert(item != NULL);

    /* jump to the first entry and probe for a match */
    pEntry = &pHashTable->pEntries[itemHash & (pHashTable->tableSize-1)];
    pEnd = &pHashTable->pEntries[pHashTable->tableSize];
    while (pEntry->data != NULL) {
        if (pEntry->data != HASH_TOMBSTONE &&
            pEntry->hashValue == itemHash &&
            (*cmpFunc)(pEntry->data, item) == 0)
        {
            /* match */
            break;
        }

        pEntry++;
        if (pEntry == pEnd) {     /* wrap around to start */
            if (pHashTable->tableSize == 1)
                break;      /* edge case - single-entry table */
            pEntry = pHashTable->pEntries;
        }
    }

    if (pEntry->data == NULL) {
        if (doAdd) {
            pEntry->hashValue = itemHash;
            pEntry->data = item;
            pHashTable->numEntries++;

            /*
             * We've added an entry.  See if this brings us too close to full.
             */
            if ((pHashTable->numEntries+pHashTable->numDeadEntries) * LOAD_DENOM
                > pHashTable->tableSize * LOAD_NUMER)
            {
                if (!resizeHash(pHashTable, pHashTable->tableSize * 2)) {
                    /* don't really have a way to indicate failure */
                    LOGE("Dalvik hash resize failure\n");
                    abort();
                }
                /* note "pEntry" is now invalid */
            }

            /* full table is bad -- search for nonexistent never halts */
            assert(pHashTable->numEntries < pHashTable->tableSize);
            result = item;
        } else {
            assert(result == NULL);
        }
    } else {
        result = pEntry->data;
    }

    return result;
}

/*
 * Remove an entry from the table.
 *
 * Does NOT invoke the "free" function on the item.
 */
bool mzHashTableRemove(HashTable* pHashTable, unsigned int itemHash, void* item)
{
    HashEntry* pEntry;
    HashEntry* pEnd;

    assert(pHashTable->tableSize > 0);

    /* jump to the first entry and probe for a match */
    pEntry = &pHashTable->pEntries[itemHash & (pHashTable->tableSize-1)];
    pEnd = &pHashTable->pEntries[pHashTable->tableSize];
    while (pEntry->data != NULL) {
        if (pEntry->data == item) {
            pEntry->data = HASH_TOMBSTONE;
            pHashTable->numEntries--;
            pHashTable->numDeadEntries++;
            return true;
        }

        pEntry++;
        if (pEntry == pEnd) {     /* wrap around to start */
            if (pHashTable->tableSize == 1)
                break;      /* edge case - single-entry table */
            pEntry = pHashTable->pEntries;
        }
    }

    return false;
}

/*
 * Execute a function on every entry in the hash table.
 *
 * If "func" returns a nonzero value, terminate early and return the value.
 */
int mzHashForeach(HashTable* pHashTable, HashForeachFunc func, void* arg)
{
    int i, val;

    for (i = 0; i < pHashTable->tableSize; i++) {
        HashEntry* pEnt = &pHashTable->pEntries[i];

        if (pEnt->data != NULL && pEnt->data != HASH_TOMBSTONE) {
            val = (*func)(pEnt->data, arg);
            if (val != 0)
                return val;
        }
    }

    return 0;
}


/*
 * Look up an entry, counting the number of times we have to probe.
 *
 * Returns -1 if the entry wasn't found.
 */
int countProbes(HashTable* pHashTable, unsigned int itemHash, const void* item,
    HashCompareFunc cmpFunc)
{
    HashEntry* pEntry;
    HashEntry* pEnd;
    int count = 0;

    assert(pHashTable->tableSize > 0);
    assert(item != HASH_TOMBSTONE);
    assert(item != NULL);

    /* jump to the first entry and probe for a match */
    pEntry = &pHashTable->pEntries[itemHash & (pHashTable->tableSize-1)];
    pEnd = &pHashTable->pEntries[pHashTable->tableSize];
    while (pEntry->data != NULL) {
        if (pEntry->data != HASH_TOMBSTONE &&
            pEntry->hashValue == itemHash &&
            (*cmpFunc)(pEntry->data, item) == 0)
        {
            /* match */
            break;
        }

        pEntry++;
        if (pEntry == pEnd) {     /* wrap around to start */
            if (pHashTable->tableSize == 1)
                break;      /* edge case - single-entry table */
            pEntry = pHashTable->pEntries;
        }

        count++;
    }
    if (pEntry->data == NULL)
        return -1;

    return count;
}

/*
 * Evaluate the amount of probing required for the specified hash table.
 *
 * We do this by running through all entries in the hash table, computing
 * the hash value and then doing a lookup.
 *
 * The caller should lock the table before calling here.
 */
void mzHashTableProbeCount(HashTable* pHashTable, HashCalcFunc calcFunc,
    HashCompareFunc cmpFunc)
{
    int numEntries, minProbe, maxProbe, totalProbe;
    HashIter iter;

    numEntries = maxProbe = totalProbe = 0;
    minProbe = 65536*32767;

    for (mzHashIterBegin(pHashTable, &iter); !mzHashIterDone(&iter);
        mzHashIterNext(&iter))
    {
        const void* data = (const void*)mzHashIterData(&iter);
        int count;

        count = countProbes(pHashTable, (*calcFunc)(data), data, cmpFunc);

        numEntries++;

        if (count < minProbe)
            minProbe = count;
        if (count > maxProbe)
            maxProbe = count;
        totalProbe += count;
    }

    LOGV("Probe: min=%d max=%d, total=%d in %d (%d), avg=%.3f\n",
        minProbe, maxProbe, totalProbe, numEntries, pHashTable->tableSize,
        (float) totalProbe / (float) numEntries);
}
