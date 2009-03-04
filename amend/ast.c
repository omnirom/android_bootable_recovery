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

#include <stdio.h>
#include "ast.h"

static const char gSpaces[] =
    "                                                                "
    "                                                                "
    "                                                                "
    "                                                                "
    "                                                                "
    "                                                                "
    "                                                                ";
const int gSpacesMax = sizeof(gSpaces) - 1;

static const char *
pad(int level)
{
    level *= 4;
    if (level > gSpacesMax) {
        level = gSpacesMax;
    }
    return gSpaces + gSpacesMax - level;
}

void dumpBooleanValue(int level, const AmBooleanValue *booleanValue);
void dumpStringValue(int level, const AmStringValue *stringValue);

void
dumpBooleanExpression(int level, const AmBooleanExpression *booleanExpression)
{
    const char *op;
    bool unary = false;

    switch (booleanExpression->op) {
    case AM_BOP_NOT:
        op = "NOT";
        unary = true;
        break;
    case AM_BOP_EQ:
        op = "EQ";
        break;
    case AM_BOP_NE:
        op = "NE";
        break;
    case AM_BOP_AND:
        op = "AND";
        break;
    case AM_BOP_OR:
        op = "OR";
        break;
    default:
        op = "??";
        break;
    }

    printf("%sBOOLEAN %s {\n", pad(level), op);
    dumpBooleanValue(level + 1, booleanExpression->arg1);
    if (!unary) {
        dumpBooleanValue(level + 1, booleanExpression->arg2);
    }
    printf("%s}\n", pad(level));
}

void
dumpFunctionArguments(int level, const AmFunctionArguments *functionArguments)
{
    int i;
    for (i = 0; i < functionArguments->argc; i++) {
        dumpStringValue(level, &functionArguments->argv[i]);
    }
}

void
dumpFunctionCall(int level, const AmFunctionCall *functionCall)
{
    printf("%sFUNCTION %s (\n", pad(level), functionCall->name);
    dumpFunctionArguments(level + 1, functionCall->args);
    printf("%s)\n", pad(level));
}

void
dumpStringValue(int level, const AmStringValue *stringValue)
{
    switch (stringValue->type) {
    case AM_SVAL_LITERAL:
        printf("%s\"%s\"\n", pad(level), stringValue->u.literal);
        break;
    case AM_SVAL_FUNCTION:
        dumpFunctionCall(level, stringValue->u.function);
        break;
    default:
        printf("%s<UNKNOWN SVAL TYPE %d>\n", pad(level), stringValue->type);
        break;
    }
}

void
dumpStringComparisonExpression(int level,
        const AmStringComparisonExpression *stringComparisonExpression)
{
    const char *op;

    switch (stringComparisonExpression->op) {
    case AM_SOP_LT:
        op = "LT";
        break;
    case AM_SOP_LE:
        op = "LE";
        break;
    case AM_SOP_GT:
        op = "GT";
        break;
    case AM_SOP_GE:
        op = "GE";
        break;
    case AM_SOP_EQ:
        op = "EQ";
        break;
    case AM_SOP_NE:
        op = "NE";
        break;
    default:
        op = "??";
        break;
    }
    printf("%sSTRING %s {\n", pad(level), op);
    dumpStringValue(level + 1, stringComparisonExpression->arg1);
    dumpStringValue(level + 1, stringComparisonExpression->arg2);
    printf("%s}\n", pad(level));
}

void
dumpBooleanValue(int level, const AmBooleanValue *booleanValue)
{
    switch (booleanValue->type) {
    case AM_BVAL_EXPRESSION:
        dumpBooleanExpression(level, &booleanValue->u.expression);
        break;
    case AM_BVAL_STRING_COMPARISON:
        dumpStringComparisonExpression(level,
                &booleanValue->u.stringComparison);
        break;
    default:
        printf("%s<UNKNOWN BVAL TYPE %d>\n", pad(1), booleanValue->type);
        break;
    }
}

void
dumpWordList(const AmWordList *wordList)
{
    int i;
    for (i = 0; i < wordList->argc; i++) {
        printf("%s\"%s\"\n", pad(1), wordList->argv[i]);
    }
}

void
dumpCommandArguments(const AmCommandArguments *commandArguments)
{
    if (commandArguments->booleanArgs) {
        dumpBooleanValue(1, commandArguments->u.b);
    } else {
        dumpWordList(commandArguments->u.w);
    }
}

void
dumpCommand(const AmCommand *command)
{
    printf("command \"%s\" {\n", command->name);
    dumpCommandArguments(command->args);
    printf("}\n");
}

void
dumpCommandList(const AmCommandList *commandList)
{
    int i;
    for (i = 0; i < commandList->commandCount; i++) {
        dumpCommand(commandList->commands[i]);
    }
}
