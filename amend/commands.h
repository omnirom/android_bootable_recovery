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

#ifndef AMEND_COMMANDS_H_
#define AMEND_COMMANDS_H_

#include "permissions.h"

/* Invoke or dry-run a command.  If "permissions" is non-NULL,
 * the hook should fill it out with the list of files and operations that
 * it would need to complete its operation.  If "permissions" is NULL,
 * the hook should do the actual work specified by its arguments.
 *
 * When a command is called with non-NULL "permissions", some arguments
 * may be NULL.  A NULL argument indicates that the argument is actually
 * the output of another function, so is not known at permissions time.
 * The permissions of leaf-node functions (those that have only literal
 * strings as arguments) will get appended to the permissions of the
 * functions that call them.  However, to be completely safe, functions
 * that receive a NULL argument should request the broadest-possible
 * permissions for the range of the input argument.
 *
 * When a boolean command is called, "argc" is the boolean value and
 * "argv" is NULL.
 */
typedef int (*CommandHook)(const char *name, void *cookie,
                                int argc, const char *argv[],
                                PermissionRequestList *permissions);

int commandInit(void);
void commandCleanup(void);

/*
 * Command management
 */

struct Command;
typedef struct Command Command;

typedef enum {
    CMD_ARGS_UNKNOWN = -1,
    CMD_ARGS_BOOLEAN = 0,
    CMD_ARGS_WORDS
} CommandArgumentType;

int registerCommand(const char *name,
        CommandArgumentType argType, CommandHook hook, void *cookie);

Command *findCommand(const char *name);

CommandArgumentType getCommandArgumentType(Command *cmd);

int callCommand(Command *cmd, int argc, const char *argv[]);
int callBooleanCommand(Command *cmd, bool arg);

int getCommandPermissions(Command *cmd, int argc, const char *argv[],
        PermissionRequestList *permissions);
int getBooleanCommandPermissions(Command *cmd, bool arg,
        PermissionRequestList *permissions);

/*
 * Function management
 */

typedef int (*FunctionHook)(const char *name, void *cookie,
                                int argc, const char *argv[],
                                char **result, size_t *resultLen,
                                PermissionRequestList *permissions);

struct Function;
typedef struct Function Function;

int registerFunction(const char *name, FunctionHook hook, void *cookie);

Function *findFunction(const char *name);

int callFunction(Function *fn, int argc, const char *argv[],
        char **result, size_t *resultLen);

int getFunctionPermissions(Function *fn, int argc, const char *argv[],
        PermissionRequestList *permissions);

#endif  // AMEND_COMMANDS_H_
