/*
 * Copyright (C) 2007 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdlib.h>
#include <string.h>
#include "symtab.h"

#define DEFAULT_TABLE_SIZE 16

typedef struct {
    char *symbol;
    const void *cookie;
    unsigned int flags;
} SymbolTableEntry;

struct SymbolTable {
    SymbolTableEntry *table;
    int numEntries;
    int maxSize;
};

SymbolTable *
createSymbolTable()
{
    SymbolTable *tab;

    tab = (SymbolTable *)malloc(sizeof(SymbolTable));
    if (tab != NULL) {
        tab->numEntries = 0;
        tab->maxSize = DEFAULT_TABLE_SIZE;
        tab->table = (SymbolTableEntry *)malloc(
                            tab->maxSize * sizeof(SymbolTableEntry));
        if (tab->table == NULL) {
            free(tab);
            tab = NULL;
        }
    }
    return tab;
}

void
deleteSymbolTable(SymbolTable *tab)
{
    if (tab != NULL) {
        while (tab->numEntries > 0) {
            free(tab->table[--tab->numEntries].symbol);
        }
        free(tab->table);
    }
}

void *
findInSymbolTable(SymbolTable *tab, const char *symbol, unsigned int flags)
{
    int i;

    if (tab == NULL || symbol == NULL) {
        return NULL;
    }

    // TODO: Sort the table and binary search
    for (i = 0; i < tab->numEntries; i++) {
        if (strcmp(tab->table[i].symbol, symbol) == 0 &&
                tab->table[i].flags == flags)
        {
            return (void *)tab->table[i].cookie;
        }
    }

    return NULL;
}

int
addToSymbolTable(SymbolTable *tab, const char *symbol, unsigned int flags,
        const void *cookie)
{
    if (tab == NULL || symbol == NULL || cookie == NULL) {
        return -1;
    }

    /* Make sure that this symbol isn't already in the table.
     */
    if (findInSymbolTable(tab, symbol, flags) != NULL) {
        return -2;
    }

    /* Make sure there's enough space for the new entry.
     */
    if (tab->numEntries == tab->maxSize) {
        SymbolTableEntry *newTable;
        int newSize;

        newSize = tab->numEntries * 2;
        if (newSize < DEFAULT_TABLE_SIZE) {
            newSize = DEFAULT_TABLE_SIZE;
        }
        newTable = (SymbolTableEntry *)realloc(tab->table,
                            newSize * sizeof(SymbolTableEntry));
        if (newTable == NULL) {
            return -1;
        }
        tab->maxSize = newSize;
        tab->table = newTable;
    }

    /* Insert the new entry.
     */
    symbol = strdup(symbol);
    if (symbol == NULL) {
        return -1;
    }
    // TODO: Sort the table
    tab->table[tab->numEntries].symbol = (char *)symbol;
    tab->table[tab->numEntries].cookie = cookie;
    tab->table[tab->numEntries].flags = flags;
    tab->numEntries++;

    return 0;
}
