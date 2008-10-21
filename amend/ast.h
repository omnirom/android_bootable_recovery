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

#ifndef AMEND_AST_H_
#define AMEND_AST_H_

#include "commands.h"

typedef struct AmStringValue AmStringValue;

typedef struct {
    int argc;
    AmStringValue *argv;
} AmFunctionArguments;

/* An internal structure used only by the parser;
 * will not appear in the output AST.
xxx try to move this into parser.h
 */
typedef struct AmFunctionArgumentBuilder AmFunctionArgumentBuilder;
struct AmFunctionArgumentBuilder {
    AmFunctionArgumentBuilder *next;
    AmStringValue *arg;
    int argCount;
};

typedef struct AmWordListBuilder AmWordListBuilder;
struct AmWordListBuilder {
    AmWordListBuilder *next;
    const char *word;
    int wordCount;
};

typedef struct {
    const char *name;
    Function *fn;
    AmFunctionArguments *args;
} AmFunctionCall;


/* <string-value> ::=
 *      <literal-string> |
 *      <function-call>
 */
struct AmStringValue {
    unsigned int line;

    enum {
        AM_SVAL_LITERAL,
        AM_SVAL_FUNCTION,
    } type;
    union {
        const char *literal;
//xxx inline instead of using pointers
        AmFunctionCall *function;
    } u;
};


/* <string-comparison-expression> ::=
 *      <string-value> <string-comparison-operator> <string-value>
 */
typedef struct {
    unsigned int line;

    enum {
        AM_SOP_LT,
        AM_SOP_LE,
        AM_SOP_GT,
        AM_SOP_GE,
        AM_SOP_EQ,
        AM_SOP_NE,
    } op;
    AmStringValue *arg1;
    AmStringValue *arg2;
} AmStringComparisonExpression;


/* <boolean-expression> ::=
 *      ! <boolean-value> |
 *      <boolean-value> <binary-boolean-operator> <boolean-value>
 */
typedef struct AmBooleanValue AmBooleanValue;
typedef struct {
    unsigned int line;

    enum {
        AM_BOP_NOT,

        AM_BOP_EQ,
        AM_BOP_NE,

        AM_BOP_AND,

        AM_BOP_OR,
    } op;
    AmBooleanValue *arg1;
    AmBooleanValue *arg2;
} AmBooleanExpression;


/* <boolean-value> ::=
 *      <boolean-expression> |
 *      <string-comparison-expression>
 */
struct AmBooleanValue {
    unsigned int line;

    enum {
        AM_BVAL_EXPRESSION,
        AM_BVAL_STRING_COMPARISON,
    } type;
    union {
        AmBooleanExpression expression;
        AmStringComparisonExpression stringComparison;
    } u;
};


typedef struct {
    unsigned int line;

    int argc;
    const char **argv;
} AmWordList;


typedef struct {
    bool booleanArgs;
    union {
        AmWordList *w;
        AmBooleanValue *b;
    } u;
} AmCommandArguments;

typedef struct {
    unsigned int line;

    const char *name;
    Command *cmd;
    AmCommandArguments *args;
} AmCommand;

typedef struct {
    AmCommand **commands;
    int commandCount;
    int arraySize;
} AmCommandList;

void dumpCommandList(const AmCommandList *commandList);

#endif  // AMEND_AST_H_
