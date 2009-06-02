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
#include <stdio.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
#include "commands.h"

#include "register.h"

#define UNUSED(p)   ((void)(p))

#define CHECK_BOOL() \
    do { \
        assert(argv == NULL); \
        if (argv != NULL) return -1; \
        assert(argc == true || argc == false); \
        if (argc != true && argc != false) return -1; \
    } while (false)

#define CHECK_WORDS() \
    do { \
        assert(argc >= 0); \
        if (argc < 0) return -1; \
        assert(argc == 0 || argv != NULL); \
        if (argc != 0 && argv == NULL) return -1; \
    } while (false)

#define CHECK_FN() \
    do { \
        CHECK_WORDS(); \
        assert(result != NULL);            \
        if (result == NULL) return -1;     \
    } while (false)


/*
 * Command definitions
 */

/* assert <boolexpr>
 */
static int
cmd_assert(const char *name, void *cookie, int argc, const char *argv[])
{
    UNUSED(name);
    UNUSED(cookie);
    CHECK_BOOL();

    /* If our argument is false, return non-zero (failure)
     * If our argument is true, return zero (success)
     */
    if (argc) {
        return 0;
    } else {
        return 1;
    }
}

/* format <root>
 */
static int
cmd_format(const char *name, void *cookie, int argc, const char *argv[])
{
    UNUSED(name);
    UNUSED(cookie);
    CHECK_WORDS();
//xxx
    return -1;
}

/* copy_dir <srcdir> <dstdir>
 */
static int
cmd_copy_dir(const char *name, void *cookie, int argc, const char *argv[])
{
    UNUSED(name);
    UNUSED(cookie);
    CHECK_WORDS();
//xxx
    return -1;
}

/* mark <resource> dirty|clean
 */
static int
cmd_mark(const char *name, void *cookie, int argc, const char *argv[])
{
    UNUSED(name);
    UNUSED(cookie);
    CHECK_WORDS();
//xxx when marking, save the top-level hash at the mark point
//    so we can retry on failure.  Otherwise the hashes won't match,
//    or someone could intentionally dirty the FS to force a downgrade
//xxx
    return -1;
}

/* done
 */
static int
cmd_done(const char *name, void *cookie, int argc, const char *argv[])
{
    UNUSED(name);
    UNUSED(cookie);
    CHECK_WORDS();
//xxx
    return -1;
}

int
registerUpdateCommands()
{
    int ret;

    ret = registerCommand("assert", CMD_ARGS_BOOLEAN, cmd_assert, NULL);
    if (ret < 0) return ret;

    ret = registerCommand("copy_dir", CMD_ARGS_WORDS, cmd_copy_dir, NULL);
    if (ret < 0) return ret;

    ret = registerCommand("format", CMD_ARGS_WORDS, cmd_format, NULL);
    if (ret < 0) return ret;

    ret = registerCommand("mark", CMD_ARGS_WORDS, cmd_mark, NULL);
    if (ret < 0) return ret;

    ret = registerCommand("done", CMD_ARGS_WORDS, cmd_done, NULL);
    if (ret < 0) return ret;

    return 0;
}


/*
 * Function definitions
 */

/* update_forced()
 *
 * Returns "true" if some system setting has determined that
 * the update should happen no matter what.
 */
static int
fn_update_forced(const char *name, void *cookie, int argc, const char *argv[],
        char **result, size_t *resultLen)
{
    UNUSED(name);
    UNUSED(cookie);
    CHECK_FN();

    if (argc != 0) {
        fprintf(stderr, "%s: wrong number of arguments (%d)\n",
                name, argc);
        return 1;
    }

    //xxx check some global or property
    bool force = true;
    if (force) {
        *result = strdup("true");
    } else {
        *result = strdup("");
    }
    if (resultLen != NULL) {
        *resultLen = strlen(*result);
    }

    return 0;
}

/* get_mark(<resource>)
 *
 * Returns the current mark associated with the provided resource.
 */
static int
fn_get_mark(const char *name, void *cookie, int argc, const char *argv[],
        char **result, size_t *resultLen)
{
    UNUSED(name);
    UNUSED(cookie);
    CHECK_FN();

    if (argc != 1) {
        fprintf(stderr, "%s: wrong number of arguments (%d)\n",
                name, argc);
        return 1;
    }

    //xxx look up the value
    *result = strdup("");
    if (resultLen != NULL) {
        *resultLen = strlen(*result);
    }

    return 0;
}

/* hash_dir(<path-to-directory>)
 */
static int
fn_hash_dir(const char *name, void *cookie, int argc, const char *argv[],
        char **result, size_t *resultLen)
{
    int ret = -1;

    UNUSED(name);
    UNUSED(cookie);
    CHECK_FN();

    const char *dir;
    if (argc != 1) {
        fprintf(stderr, "%s: wrong number of arguments (%d)\n",
                name, argc);
        return 1;
    } else {
        dir = argv[0];
    }

//xxx build and return the string
    *result = strdup("hashvalue");
    if (resultLen != NULL) {
      *resultLen = strlen(*result);
    }
    ret = 0;

    return ret;
}

/* matches(<str>, <str1> [, <strN>...])
 * If <str> matches (strcmp) any of <str1>...<strN>, returns <str>,
 * otherwise returns "".
 *
 * E.g., assert matches(hash_dir("/path"), "hash1", "hash2")
 */
static int
fn_matches(const char *name, void *cookie, int argc, const char *argv[],
        char **result, size_t *resultLen)
{
    UNUSED(name);
    UNUSED(cookie);
    CHECK_FN();

    if (argc < 2) {
        fprintf(stderr, "%s: not enough arguments (%d < 2)\n",
                name, argc);
        return 1;
    }

    int i;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[0], argv[i]) == 0) {
            *result = strdup(argv[0]);
            if (resultLen != NULL) {
                *resultLen = strlen(*result);
            }
            return 0;
        }
    }

    *result = strdup("");
    if (resultLen != NULL) {
        *resultLen = 1;
    }
    return 0;
}

/* concat(<str>, <str1> [, <strN>...])
 * Returns the concatenation of all strings.
 */
static int
fn_concat(const char *name, void *cookie, int argc, const char *argv[],
        char **result, size_t *resultLen)
{
    UNUSED(name);
    UNUSED(cookie);
    CHECK_FN();

    size_t totalLen = 0;
    int i;
    for (i = 0; i < argc; i++) {
        totalLen += strlen(argv[i]);
    }

    char *s = (char *)malloc(totalLen + 1);
    if (s == NULL) {
        return -1;
    }
    s[totalLen] = '\0';
    for (i = 0; i < argc; i++) {
        //TODO: keep track of the end to avoid walking the string each time
        strcat(s, argv[i]);
    }
    *result = s;
    if (resultLen != NULL) {
        *resultLen = strlen(s);
    }

    return 0;
}

int
registerUpdateFunctions()
{
    int ret;

    ret = registerFunction("update_forced", fn_update_forced, NULL);
    if (ret < 0) return ret;

    ret = registerFunction("get_mark", fn_get_mark, NULL);
    if (ret < 0) return ret;

    ret = registerFunction("hash_dir", fn_hash_dir, NULL);
    if (ret < 0) return ret;

    ret = registerFunction("matches", fn_matches, NULL);
    if (ret < 0) return ret;

    ret = registerFunction("concat", fn_concat, NULL);
    if (ret < 0) return ret;

    return 0;
}
