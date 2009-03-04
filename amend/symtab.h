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

#ifndef AMEND_SYMTAB_H_
#define AMEND_SYMTAB_H_

typedef struct SymbolTable SymbolTable;

SymbolTable *createSymbolTable(void);

void deleteSymbolTable(SymbolTable *tab);

/* symbol and cookie must be non-NULL.
 */
int addToSymbolTable(SymbolTable *tab, const char *symbol, unsigned int flags,
        const void *cookie);

void *findInSymbolTable(SymbolTable *tab, const char *symbol,
        unsigned int flags);

#endif  // AMEND_SYMTAB_H_
