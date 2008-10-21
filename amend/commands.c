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
#include <stdio.h>
#include "symtab.h"
#include "commands.h"

#if 1
#define TRACE(...)  printf(__VA_ARGS__)
#else
#define TRACE(...)  /**/
#endif

typedef enum {
    CMD_TYPE_UNKNOWN = -1,
    CMD_TYPE_COMMAND = 0,
    CMD_TYPE_FUNCTION
} CommandType;

typedef struct {
    const char *name;
    void *cookie;
    CommandType type;
    CommandArgumentType argType;
    CommandHook hook;
} CommandEntry;

static struct {
    SymbolTable *symbolTable;
    bool commandStateInitialized;
} gCommandState;

int
commandInit()
{
    if (gCommandState.commandStateInitialized) {
        return -1;
    }
    gCommandState.symbolTable = createSymbolTable();
    if (gCommandState.symbolTable == NULL) {
        return -1;
    }
    gCommandState.commandStateInitialized = true;
    return 0;
}

void
commandCleanup()
{
    if (gCommandState.commandStateInitialized) {
        gCommandState.commandStateInitialized = false;
        deleteSymbolTable(gCommandState.symbolTable);
        gCommandState.symbolTable = NULL;
//xxx need to free the entries and names in the symbol table
    }
}

static int
registerCommandInternal(const char *name, CommandType type,
        CommandArgumentType argType, CommandHook hook, void *cookie)
{
    CommandEntry *entry;

    if (!gCommandState.commandStateInitialized) {
        return -1;
    }
    if (name == NULL || hook == NULL) {
        return -1;
    }
    if (type != CMD_TYPE_COMMAND && type != CMD_TYPE_FUNCTION) {
        return -1;
    }
    if (argType != CMD_ARGS_BOOLEAN && argType != CMD_ARGS_WORDS) {
        return -1;
    }

    entry = (CommandEntry *)malloc(sizeof(CommandEntry));
    if (entry != NULL) {
        entry->name = strdup(name);
        if (entry->name != NULL) {
            int ret;

            entry->cookie = cookie;
            entry->type = type;
            entry->argType = argType;
            entry->hook = hook;
            ret = addToSymbolTable(gCommandState.symbolTable,
                        entry->name, entry->type, entry);
            if (ret == 0) {
                return 0;
            }
        }
        free(entry);
    }

    return -1;
}

int
registerCommand(const char *name,
        CommandArgumentType argType, CommandHook hook, void *cookie)
{
    return registerCommandInternal(name,
            CMD_TYPE_COMMAND, argType, hook, cookie);
}

int
registerFunction(const char *name, FunctionHook hook, void *cookie)
{
    return registerCommandInternal(name,
            CMD_TYPE_FUNCTION, CMD_ARGS_WORDS, (CommandHook)hook, cookie);
}

Command *
findCommand(const char *name)
{
    return (Command *)findInSymbolTable(gCommandState.symbolTable,
            name, CMD_TYPE_COMMAND);
}

Function *
findFunction(const char *name)
{
    return (Function *)findInSymbolTable(gCommandState.symbolTable,
            name, CMD_TYPE_FUNCTION);
}

CommandArgumentType
getCommandArgumentType(Command *cmd)
{
    CommandEntry *entry = (CommandEntry *)cmd;

    if (entry != NULL) {
        return entry->argType;
    }
    return CMD_ARGS_UNKNOWN;
}

static int
callCommandInternal(CommandEntry *entry, int argc, const char *argv[],
        PermissionRequestList *permissions)
{
    if (entry != NULL && entry->argType == CMD_ARGS_WORDS &&
            (argc == 0 || (argc > 0 && argv != NULL)))
    {
        if (permissions == NULL) {
            int i;
            for (i = 0; i < argc; i++) {
                if (argv[i] == NULL) {
                    goto bail;
                }
            }
        }
        TRACE("calling command %s\n", entry->name);
        return entry->hook(entry->name, entry->cookie, argc, argv, permissions);
//xxx if permissions, make sure the entry has added at least one element.
    }
bail:
    return -1;
}

static int
callBooleanCommandInternal(CommandEntry *entry, bool arg,
        PermissionRequestList *permissions)
{
    if (entry != NULL && entry->argType == CMD_ARGS_BOOLEAN) {
        TRACE("calling boolean command %s\n", entry->name);
        return entry->hook(entry->name, entry->cookie, arg ? 1 : 0, NULL,
                permissions);
//xxx if permissions, make sure the entry has added at least one element.
    }
    return -1;
}

int
callCommand(Command *cmd, int argc, const char *argv[])
{
    return callCommandInternal((CommandEntry *)cmd, argc, argv, NULL);
}

int
callBooleanCommand(Command *cmd, bool arg)
{
    return callBooleanCommandInternal((CommandEntry *)cmd, arg, NULL);
}

int
getCommandPermissions(Command *cmd, int argc, const char *argv[],
        PermissionRequestList *permissions)
{
    if (permissions != NULL) {
        return callCommandInternal((CommandEntry *)cmd, argc, argv,
                permissions);
    }
    return -1;
}

int
getBooleanCommandPermissions(Command *cmd, bool arg,
        PermissionRequestList *permissions)
{
    if (permissions != NULL) {
        return callBooleanCommandInternal((CommandEntry *)cmd, arg,
                permissions);
    }
    return -1;
}

int
callFunctionInternal(CommandEntry *entry, int argc, const char *argv[],
        char **result, size_t *resultLen, PermissionRequestList *permissions)
{
    if (entry != NULL && entry->argType == CMD_ARGS_WORDS &&
            (argc == 0 || (argc > 0 && argv != NULL)))
    {
        if ((permissions == NULL && result != NULL) ||
                (permissions != NULL && result == NULL))
        {
            if (permissions == NULL) {
                /* This is the actual invocation of the function,
                 * which means that none of the arguments are allowed
                 * to be NULL.
                 */
                int i;
                for (i = 0; i < argc; i++) {
                    if (argv[i] == NULL) {
                        goto bail;
                    }
                }
            }
            TRACE("calling function %s\n", entry->name);
            return ((FunctionHook)entry->hook)(entry->name, entry->cookie,
                    argc, argv, result, resultLen, permissions);
//xxx if permissions, make sure the entry has added at least one element.
        }
    }
bail:
    return -1;
}

int
callFunction(Function *fn, int argc, const char *argv[],
        char **result, size_t *resultLen)
{
    return callFunctionInternal((CommandEntry *)fn, argc, argv,
            result, resultLen, NULL);
}

int
getFunctionPermissions(Function *fn, int argc, const char *argv[],
        PermissionRequestList *permissions)
{
    if (permissions != NULL) {
        return callFunctionInternal((CommandEntry *)fn, argc, argv,
                NULL, NULL, permissions);
    }
    return -1;
}
