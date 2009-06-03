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
#undef NDEBUG
#include <assert.h>
#include "commands.h"

static struct {
    bool called;
    const char *name;
    void *cookie;
    int argc;
    const char **argv;
    int returnValue;
    char *functionResult;
} gTestCommandState;

static int
testCommand(const char *name, void *cookie, int argc, const char *argv[])
{
    gTestCommandState.called = true;
    gTestCommandState.name = name;
    gTestCommandState.cookie = cookie;
    gTestCommandState.argc = argc;
    gTestCommandState.argv = argv;
    return gTestCommandState.returnValue;
}

static int
testFunction(const char *name, void *cookie, int argc, const char *argv[],
        char **result, size_t *resultLen)
{
    gTestCommandState.called = true;
    gTestCommandState.name = name;
    gTestCommandState.cookie = cookie;
    gTestCommandState.argc = argc;
    gTestCommandState.argv = argv;
    if (result != NULL) {
        *result = gTestCommandState.functionResult;
        if (resultLen != NULL) {
            *resultLen = strlen(*result);
        }
    }
    return gTestCommandState.returnValue;
}

static int
test_commands()
{
    Command *cmd;
    int ret;
    CommandArgumentType argType;

    ret = commandInit();
    assert(ret == 0);

    /* Make sure we can't initialize twice.
     */
    ret = commandInit();
    assert(ret < 0);

    /* Try calling with some bad values.
     */
    ret = registerCommand(NULL, CMD_ARGS_UNKNOWN, NULL, NULL);
    assert(ret < 0);

    ret = registerCommand("hello", CMD_ARGS_UNKNOWN, NULL, NULL);
    assert(ret < 0);

    ret = registerCommand("hello", CMD_ARGS_WORDS, NULL, NULL);
    assert(ret < 0);

    cmd = findCommand(NULL);
    assert(cmd == NULL);

    argType = getCommandArgumentType(NULL);
    assert((int)argType < 0);

    ret = callCommand(NULL, -1, NULL);
    assert(ret < 0);

    ret = callBooleanCommand(NULL, false);
    assert(ret < 0);

    /* Register some commands.
     */
    ret = registerCommand("one", CMD_ARGS_WORDS, testCommand,
            &gTestCommandState);
    assert(ret == 0);

    ret = registerCommand("two", CMD_ARGS_WORDS, testCommand,
            &gTestCommandState);
    assert(ret == 0);

    ret = registerCommand("bool", CMD_ARGS_BOOLEAN, testCommand,
            &gTestCommandState);
    assert(ret == 0);

    /* Make sure that all of those commands exist and that their
     * argument types are correct.
     */
    cmd = findCommand("one");
    assert(cmd != NULL);
    argType = getCommandArgumentType(cmd);
    assert(argType == CMD_ARGS_WORDS);

    cmd = findCommand("two");
    assert(cmd != NULL);
    argType = getCommandArgumentType(cmd);
    assert(argType == CMD_ARGS_WORDS);

    cmd = findCommand("bool");
    assert(cmd != NULL);
    argType = getCommandArgumentType(cmd);
    assert(argType == CMD_ARGS_BOOLEAN);

    /* Make sure that no similar commands exist.
     */
    cmd = findCommand("on");
    assert(cmd == NULL);

    cmd = findCommand("onee");
    assert(cmd == NULL);

    /* Make sure that a double insertion fails.
     */
    ret = registerCommand("one", CMD_ARGS_WORDS, testCommand,
            &gTestCommandState);
    assert(ret < 0);

    /* Make sure that bad args fail.
     */
    cmd = findCommand("one");
    assert(cmd != NULL);

    ret = callCommand(cmd, -1, NULL);   // argc must be non-negative
    assert(ret < 0);

    ret = callCommand(cmd, 1, NULL);    // argv can't be NULL if argc > 0
    assert(ret < 0);

    /* Make sure that you can't make a boolean call on a regular command.
     */
    cmd = findCommand("one");
    assert(cmd != NULL);

    ret = callBooleanCommand(cmd, false);
    assert(ret < 0);

    /* Make sure that you can't make a regular call on a boolean command.
     */
    cmd = findCommand("bool");
    assert(cmd != NULL);

    ret = callCommand(cmd, 0, NULL);
    assert(ret < 0);

    /* Set up some arguments.
     */
    int argc = 4;
    const char *argv[4] = { "ONE", "TWO", "THREE", "FOUR" };

    /* Make a call and make sure that it occurred.
     */
    cmd = findCommand("one");
    assert(cmd != NULL);
    memset(&gTestCommandState, 0, sizeof(gTestCommandState));
    gTestCommandState.called = false;
    gTestCommandState.returnValue = 25;
    ret = callCommand(cmd, argc, argv);
//xxx also try calling with a null argv element (should fail)
    assert(ret == 25);
    assert(gTestCommandState.called);
    assert(strcmp(gTestCommandState.name, "one") == 0);
    assert(gTestCommandState.cookie == &gTestCommandState);
    assert(gTestCommandState.argc == argc);
    assert(gTestCommandState.argv == argv);

    /* Make a boolean call and make sure that it occurred.
     */
    cmd = findCommand("bool");
    assert(cmd != NULL);

    memset(&gTestCommandState, 0, sizeof(gTestCommandState));
    gTestCommandState.called = false;
    gTestCommandState.returnValue = 12;
    ret = callBooleanCommand(cmd, false);
    assert(ret == 12);
    assert(gTestCommandState.called);
    assert(strcmp(gTestCommandState.name, "bool") == 0);
    assert(gTestCommandState.cookie == &gTestCommandState);
    assert(gTestCommandState.argc == 0);
    assert(gTestCommandState.argv == NULL);

    memset(&gTestCommandState, 0, sizeof(gTestCommandState));
    gTestCommandState.called = false;
    gTestCommandState.returnValue = 13;
    ret = callBooleanCommand(cmd, true);
    assert(ret == 13);
    assert(gTestCommandState.called);
    assert(strcmp(gTestCommandState.name, "bool") == 0);
    assert(gTestCommandState.cookie == &gTestCommandState);
    assert(gTestCommandState.argc == 1);
    assert(gTestCommandState.argv == NULL);

    /* Smoke test commandCleanup().
     */
    commandCleanup();

    return 0;
}

static int
test_functions()
{
    Function *fn;
    int ret;

    ret = commandInit();
    assert(ret == 0);

    /* Try calling with some bad values.
     */
    ret = registerFunction(NULL, NULL, NULL);
    assert(ret < 0);

    ret = registerFunction("hello", NULL, NULL);
    assert(ret < 0);

    fn = findFunction(NULL);
    assert(fn == NULL);

    ret = callFunction(NULL, -1, NULL, NULL, NULL);
    assert(ret < 0);

    /* Register some functions.
     */
    ret = registerFunction("one", testFunction, &gTestCommandState);
    assert(ret == 0);

    ret = registerFunction("two", testFunction, &gTestCommandState);
    assert(ret == 0);

    ret = registerFunction("three", testFunction, &gTestCommandState);
    assert(ret == 0);

    /* Make sure that all of those functions exist.
     * argument types are correct.
     */
    fn = findFunction("one");
    assert(fn != NULL);

    fn = findFunction("two");
    assert(fn != NULL);

    fn = findFunction("three");
    assert(fn != NULL);

    /* Make sure that no similar functions exist.
     */
    fn = findFunction("on");
    assert(fn == NULL);

    fn = findFunction("onee");
    assert(fn == NULL);

    /* Make sure that a double insertion fails.
     */
    ret = registerFunction("one", testFunction, &gTestCommandState);
    assert(ret < 0);

    /* Make sure that bad args fail.
     */
    fn = findFunction("one");
    assert(fn != NULL);

    // argc must be non-negative
    ret = callFunction(fn, -1, NULL, (char **)1, NULL);
    assert(ret < 0);

    // argv can't be NULL if argc > 0
    ret = callFunction(fn, 1, NULL, (char **)1, NULL);
    assert(ret < 0);

    // result can't be NULL
    ret = callFunction(fn, 0, NULL, NULL, NULL);
    assert(ret < 0);

    /* Set up some arguments.
     */
    int argc = 4;
    const char *argv[4] = { "ONE", "TWO", "THREE", "FOUR" };

    /* Make a call and make sure that it occurred.
     */
    char *functionResult;
    size_t functionResultLen;
    fn = findFunction("one");
    assert(fn != NULL);
    memset(&gTestCommandState, 0, sizeof(gTestCommandState));
    gTestCommandState.called = false;
    gTestCommandState.returnValue = 25;
    gTestCommandState.functionResult = "1234";
    functionResult = NULL;
    functionResultLen = 55;
    ret = callFunction(fn, argc, argv,
            &functionResult, &functionResultLen);
//xxx also try calling with a null resultLen arg (should succeed)
//xxx also try calling with a null argv element (should fail)
    assert(ret == 25);
    assert(gTestCommandState.called);
    assert(strcmp(gTestCommandState.name, "one") == 0);
    assert(gTestCommandState.cookie == &gTestCommandState);
    assert(gTestCommandState.argc == argc);
    assert(gTestCommandState.argv == argv);
    assert(strcmp(functionResult, "1234") == 0);
    assert(functionResultLen == strlen(functionResult));

    /* Smoke test commandCleanup().
     */
    commandCleanup();

    return 0;
}

static int
test_interaction()
{
    Command *cmd;
    Function *fn;
    int ret;

    ret = commandInit();
    assert(ret == 0);

    /* Register some commands.
     */
    ret = registerCommand("one", CMD_ARGS_WORDS, testCommand, (void *)0xc1);
    assert(ret == 0);

    ret = registerCommand("two", CMD_ARGS_WORDS, testCommand, (void *)0xc2);
    assert(ret == 0);

    /* Register some functions, one of which shares a name with a command.
     */
    ret = registerFunction("one", testFunction, (void *)0xf1);
    assert(ret == 0);

    ret = registerFunction("three", testFunction, (void *)0xf3);
    assert(ret == 0);

    /* Look up each of the commands, and make sure no command exists
     * with the name used only by our function.
     */
    cmd = findCommand("one");
    assert(cmd != NULL);

    cmd = findCommand("two");
    assert(cmd != NULL);

    cmd = findCommand("three");
    assert(cmd == NULL);

    /* Look up each of the functions, and make sure no function exists
     * with the name used only by our command.
     */
    fn = findFunction("one");
    assert(fn != NULL);

    fn = findFunction("two");
    assert(fn == NULL);

    fn = findFunction("three");
    assert(fn != NULL);

    /* Set up some arguments.
     */
    int argc = 4;
    const char *argv[4] = { "ONE", "TWO", "THREE", "FOUR" };

    /* Call the overlapping command and make sure that the cookie is correct.
     */
    cmd = findCommand("one");
    assert(cmd != NULL);
    memset(&gTestCommandState, 0, sizeof(gTestCommandState));
    gTestCommandState.called = false;
    gTestCommandState.returnValue = 123;
    ret = callCommand(cmd, argc, argv);
    assert(ret == 123);
    assert(gTestCommandState.called);
    assert(strcmp(gTestCommandState.name, "one") == 0);
    assert((int)gTestCommandState.cookie == 0xc1);
    assert(gTestCommandState.argc == argc);
    assert(gTestCommandState.argv == argv);

    /* Call the overlapping function and make sure that the cookie is correct.
     */
    char *functionResult;
    size_t functionResultLen;
    fn = findFunction("one");
    assert(fn != NULL);
    memset(&gTestCommandState, 0, sizeof(gTestCommandState));
    gTestCommandState.called = false;
    gTestCommandState.returnValue = 125;
    gTestCommandState.functionResult = "5678";
    functionResult = NULL;
    functionResultLen = 66;
    ret = callFunction(fn, argc, argv, &functionResult, &functionResultLen);
    assert(ret == 125);
    assert(gTestCommandState.called);
    assert(strcmp(gTestCommandState.name, "one") == 0);
    assert((int)gTestCommandState.cookie == 0xf1);
    assert(gTestCommandState.argc == argc);
    assert(gTestCommandState.argv == argv);
    assert(strcmp(functionResult, "5678") == 0);
    assert(functionResultLen == strlen(functionResult));

    /* Clean up.
     */
    commandCleanup();

    return 0;
}

int
test_cmd_fn()
{
    int ret;

    ret = test_commands();
    if (ret != 0) {
        fprintf(stderr, "test_commands() failed: %d\n", ret);
        return ret;
    }

    ret = test_functions();
    if (ret != 0) {
        fprintf(stderr, "test_functions() failed: %d\n", ret);
        return ret;
    }

    ret = test_interaction();
    if (ret != 0) {
        fprintf(stderr, "test_interaction() failed: %d\n", ret);
        return ret;
    }

    return 0;
}
