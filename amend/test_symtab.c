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
#undef NDEBUG
#include <assert.h>
#include "symtab.h"

int
test_symtab()
{
    SymbolTable *tab;
    void *cookie;
    int ret;

    /* Test creation */
    tab = createSymbolTable();
    assert(tab != NULL);

    /* Smoke-test deletion */
    deleteSymbolTable(tab);


    tab = createSymbolTable();
    assert(tab != NULL);


    /* table parameter must be non-NULL. */
    ret = addToSymbolTable(NULL, NULL, 0, NULL);
    assert(ret < 0);

    /* symbol parameter must be non-NULL. */
    ret = addToSymbolTable(tab, NULL, 0, NULL);
    assert(ret < 0);
    
    /* cookie parameter must be non-NULL. */
    ret = addToSymbolTable(tab, "null", 0, NULL);
    assert(ret < 0);


    /* table parameter must be non-NULL. */
    cookie = findInSymbolTable(NULL, NULL, 0);
    assert(cookie == NULL);

    /* symbol parameter must be non-NULL. */
    cookie = findInSymbolTable(tab, NULL, 0);
    assert(cookie == NULL);


    /* Try some actual inserts.
     */
    ret = addToSymbolTable(tab, "one", 0, (void *)1);
    assert(ret == 0);

    ret = addToSymbolTable(tab, "two", 0, (void *)2);
    assert(ret == 0);

    ret = addToSymbolTable(tab, "three", 0, (void *)3);
    assert(ret == 0);

    /* Try some lookups.
     */
    cookie = findInSymbolTable(tab, "one", 0);
    assert((int)cookie == 1);

    cookie = findInSymbolTable(tab, "two", 0);
    assert((int)cookie == 2);

    cookie = findInSymbolTable(tab, "three", 0);
    assert((int)cookie == 3);

    /* Try to insert something that's already there.
     */
    ret = addToSymbolTable(tab, "one", 0, (void *)1111);
    assert(ret < 0);

    /* Make sure that the failed duplicate insert didn't
     * clobber the original cookie value.
     */
    cookie = findInSymbolTable(tab, "one", 0);
    assert((int)cookie == 1);

    /* Try looking up something that isn't there.
     */
    cookie = findInSymbolTable(tab, "FOUR", 0);
    assert(cookie == NULL);

    /* Try looking up something that's similar to an existing entry.
     */
    cookie = findInSymbolTable(tab, "on", 0);
    assert(cookie == NULL);

    cookie = findInSymbolTable(tab, "onee", 0);
    assert(cookie == NULL);

    /* Test flags.
     * Try inserting something with a different flag.
     */
    ret = addToSymbolTable(tab, "ten", 333, (void *)10);
    assert(ret == 0);

    /* Make sure it's there.
     */
    cookie = findInSymbolTable(tab, "ten", 333);
    assert((int)cookie == 10);

    /* Make sure it's not there when looked up with a different flag.
     */
    cookie = findInSymbolTable(tab, "ten", 0);
    assert(cookie == NULL);

    /* Try inserting something that has the same name as something
     * with a different flag.
     */
    ret = addToSymbolTable(tab, "one", 333, (void *)11);
    assert(ret == 0);

    /* Make sure the new entry exists.
     */
    cookie = findInSymbolTable(tab, "one", 333);
    assert((int)cookie == 11);

    /* Make sure the old entry still has the right value.
     */
    cookie = findInSymbolTable(tab, "one", 0);
    assert((int)cookie == 1);

    /* Try deleting again, now that there's stuff in the table.
     */
    deleteSymbolTable(tab);

    return 0;
}
