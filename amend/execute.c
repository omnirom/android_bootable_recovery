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
#include "ast.h"
#include "execute.h"

typedef struct {
    int c;
    const char **v;
} StringList;

static int execBooleanValue(ExecContext *ctx,
        const AmBooleanValue *booleanValue, bool *result);
static int execStringValue(ExecContext *ctx, const AmStringValue *stringValue,
        const char **result);

static int
execBooleanExpression(ExecContext *ctx,
        const AmBooleanExpression *booleanExpression, bool *result)
{
    int ret;
    bool arg1, arg2;
    bool unary;

    assert(ctx != NULL);
    assert(booleanExpression != NULL);
    assert(result != NULL);
    if (ctx == NULL || booleanExpression == NULL || result == NULL) {
        return -__LINE__;
    }

    if (booleanExpression->op == AM_BOP_NOT) {
        unary = true;
    } else {
        unary = false;
    }

    ret = execBooleanValue(ctx, booleanExpression->arg1, &arg1);
    if (ret != 0) return ret;

    if (!unary) {
        ret = execBooleanValue(ctx, booleanExpression->arg2, &arg2);
        if (ret != 0) return ret;
    } else {
        arg2 = false;
    }

    switch (booleanExpression->op) {
    case AM_BOP_NOT:
        *result = !arg1;
        break;
    case AM_BOP_EQ:
        *result = (arg1 == arg2);
        break;
    case AM_BOP_NE:
        *result = (arg1 != arg2);
        break;
    case AM_BOP_AND:
        *result = (arg1 && arg2);
        break;
    case AM_BOP_OR:
        *result = (arg1 || arg2);
        break;
    default:
        return -__LINE__;
    }

    return 0;
}

static int
execFunctionArguments(ExecContext *ctx,
        const AmFunctionArguments *functionArguments, StringList *result)
{
    int ret;

    assert(ctx != NULL);
    assert(functionArguments != NULL);
    assert(result != NULL);
    if (ctx == NULL || functionArguments == NULL || result == NULL) {
        return -__LINE__;
    }

    result->c = functionArguments->argc;
    result->v = (const char **)malloc(result->c * sizeof(const char *));
    if (result->v == NULL) {
        result->c = 0;
        return -__LINE__;
    }

    int i;
    for (i = 0; i < functionArguments->argc; i++) {
        ret = execStringValue(ctx, &functionArguments->argv[i], &result->v[i]);
        if (ret != 0) {
            result->c = 0;
            free(result->v);
            //TODO: free the individual args, if we're responsible for them.
            result->v = NULL;
            return ret;
        }
    }

    return 0;
}

static int
execFunctionCall(ExecContext *ctx, const AmFunctionCall *functionCall,
        const char **result)
{
    int ret;

    assert(ctx != NULL);
    assert(functionCall != NULL);
    assert(result != NULL);
    if (ctx == NULL || functionCall == NULL || result == NULL) {
        return -__LINE__;
    }

    StringList args;
    ret = execFunctionArguments(ctx, functionCall->args, &args);
    if (ret != 0) {
        return ret;
    }

    ret = callFunction(functionCall->fn, args.c, args.v, (char **)result, NULL);
    if (ret != 0) {
        return ret;
    }

    //TODO: clean up args

    return 0;
}

static int
execStringValue(ExecContext *ctx, const AmStringValue *stringValue,
        const char **result)
{
    int ret;

    assert(ctx != NULL);
    assert(stringValue != NULL);
    assert(result != NULL);
    if (ctx == NULL || stringValue == NULL || result == NULL) {
        return -__LINE__;
    }

    switch (stringValue->type) {
    case AM_SVAL_LITERAL:
        *result = strdup(stringValue->u.literal);
        break;
    case AM_SVAL_FUNCTION:
        ret = execFunctionCall(ctx, stringValue->u.function, result);
        if (ret != 0) {
            return ret;
        }
        break;
    default:
        return -__LINE__;
    }

    return 0;
}

static int
execStringComparisonExpression(ExecContext *ctx,
        const AmStringComparisonExpression *stringComparisonExpression,
        bool *result)
{
    int ret;

    assert(ctx != NULL);
    assert(stringComparisonExpression != NULL);
    assert(result != NULL);
    if (ctx == NULL || stringComparisonExpression == NULL || result == NULL) {
        return -__LINE__;
    }

    const char *arg1, *arg2;
    ret = execStringValue(ctx, stringComparisonExpression->arg1, &arg1);
    if (ret != 0) {
        return ret;
    }
    ret = execStringValue(ctx, stringComparisonExpression->arg2, &arg2);
    if (ret != 0) {
        return ret;
    }

    int cmp = strcmp(arg1, arg2);

    switch (stringComparisonExpression->op) {
    case AM_SOP_LT:
        *result = (cmp < 0);
        break;
    case AM_SOP_LE:
        *result = (cmp <= 0);
        break;
    case AM_SOP_GT:
        *result = (cmp > 0);
        break;
    case AM_SOP_GE:
        *result = (cmp >= 0);
        break;
    case AM_SOP_EQ:
        *result = (cmp == 0);
        break;
    case AM_SOP_NE:
        *result = (cmp != 0);
        break;
    default:
        return -__LINE__;
        break;
    }

    return 0;
}

static int
execBooleanValue(ExecContext *ctx, const AmBooleanValue *booleanValue,
        bool *result)
{
    int ret;

    assert(ctx != NULL);
    assert(booleanValue != NULL);
    assert(result != NULL);
    if (ctx == NULL || booleanValue == NULL || result == NULL) {
        return -__LINE__;
    }

    switch (booleanValue->type) {
    case AM_BVAL_EXPRESSION:
        ret = execBooleanExpression(ctx, &booleanValue->u.expression, result);
        break;
    case AM_BVAL_STRING_COMPARISON:
        ret = execStringComparisonExpression(ctx,
                &booleanValue->u.stringComparison, result);
        break;
    default:
        ret = -__LINE__;
        break;
    }

    return ret;
}

static int
execCommand(ExecContext *ctx, const AmCommand *command)
{
    int ret;

    assert(ctx != NULL);
    assert(command != NULL);
    if (ctx == NULL || command == NULL) {
        return -__LINE__;
    }

    CommandArgumentType argType;
    argType = getCommandArgumentType(command->cmd);
    switch (argType) {
    case CMD_ARGS_BOOLEAN:
        {
            bool bVal;
            ret = execBooleanValue(ctx, command->args->u.b, &bVal);
            if (ret == 0) {
                ret = callBooleanCommand(command->cmd, bVal);
            }
        }
        break;
    case CMD_ARGS_WORDS:
        {
            AmWordList *words = command->args->u.w;
            ret = callCommand(command->cmd, words->argc, words->argv);
        }
        break;
    default:
        ret = -__LINE__;
        break;
    }

    return ret;
}

int
execCommandList(ExecContext *ctx, const AmCommandList *commandList)
{
    int i;
    for (i = 0; i < commandList->commandCount; i++) {
        int ret = execCommand(ctx, commandList->commands[i]);
        if (ret != 0) {
            int line = commandList->commands[i]->line;
            return line > 0 ? line : ret;
        }
    }

    return 0;
}
