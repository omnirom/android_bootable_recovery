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

%{
#undef NDEBUG
    #include <stdlib.h>
    #include <string.h>
    #include <assert.h>
    #include <stdio.h>
    #include "ast.h"
    #include "lexer.h"
    #include "commands.h"

    void yyerror(const char *msg);
    int yylex(void);

#define STRING_COMPARISON(out, a1, sop, a2) \
    do { \
        out = (AmBooleanValue *)malloc(sizeof(AmBooleanValue)); \
        if (out == NULL) { \
            YYABORT; \
        } \
        out->type = AM_BVAL_STRING_COMPARISON; \
        out->u.stringComparison.op = sop; \
        out->u.stringComparison.arg1 = a1; \
        out->u.stringComparison.arg2 = a2; \
    } while (false)

#define BOOLEAN_EXPRESSION(out, a1, bop, a2) \
    do { \
        out = (AmBooleanValue *)malloc(sizeof(AmBooleanValue)); \
        if (out == NULL) { \
            YYABORT; \
        } \
        out->type = AM_BVAL_EXPRESSION; \
        out->u.expression.op = bop; \
        out->u.expression.arg1 = a1; \
        out->u.expression.arg2 = a2; \
    } while (false)

AmCommandList *gCommands = NULL;
%}

%start  lines

%union  {
        char *literalString;
        AmFunctionArgumentBuilder *functionArgumentBuilder;
        AmFunctionArguments *functionArguments;
        AmFunctionCall *functionCall;
        AmStringValue *stringValue;
        AmBooleanValue *booleanValue;
        AmWordListBuilder *wordListBuilder;
        AmCommandArguments *commandArguments;
        AmCommand *command;
        AmCommandList *commandList;
    }

%token  TOK_AND TOK_OR TOK_EQ TOK_NE TOK_GE TOK_LE TOK_EOF TOK_EOL TOK_ERROR
%token  <literalString> TOK_STRING TOK_IDENTIFIER TOK_WORD

%type   <commandList> lines
%type   <command> command line
%type   <functionArgumentBuilder> function_arguments
%type   <functionArguments> function_arguments_or_empty
%type   <functionCall> function_call
%type   <literalString> function_name
%type   <stringValue> string_value
%type   <booleanValue> boolean_expression
%type   <wordListBuilder> word_list
%type   <commandArguments> arguments

/* Operator precedence, weakest to strongest.
 * Same as C/Java precedence.
 */

%left   TOK_OR
%left   TOK_AND
%left   TOK_EQ TOK_NE
%left   '<' '>' TOK_LE TOK_GE
%right   '!'

%%

lines :     /* empty */
                {
                    $$ = (AmCommandList *)malloc(sizeof(AmCommandList));
                    if ($$ == NULL) {
                        YYABORT;
                    }
gCommands = $$;
                    $$->arraySize = 64;
                    $$->commandCount = 0;
                    $$->commands = (AmCommand **)malloc(
                            sizeof(AmCommand *) * $$->arraySize);
                    if ($$->commands == NULL) {
                        YYABORT;
                    }
                }
        |   lines line
                {
                    if ($2 != NULL) {
                        if ($1->commandCount >= $1->arraySize) {
                            AmCommand **newArray;
                            newArray = (AmCommand **)realloc($$->commands,
                                sizeof(AmCommand *) * $$->arraySize * 2);
                            if (newArray == NULL) {
                                YYABORT;
                            }
                            $$->commands = newArray;
                            $$->arraySize *= 2;
                        }
                        $1->commands[$1->commandCount++] = $2;
                    }
                }
        ;

line :      line_ending
                {
                    $$ = NULL;  /* ignore blank lines */
                }
        |   command arguments line_ending
                {
                    $$ = $1;
                    $$->args = $2;
                    setLexerArgumentType(AM_UNKNOWN_ARGS);
                }
        ;

command :   TOK_IDENTIFIER
                {
                    Command *cmd = findCommand($1);
                    if (cmd == NULL) {
                        fprintf(stderr, "Unknown command \"%s\"\n", $1);
                        YYABORT;
                    }
                    $$ = (AmCommand *)malloc(sizeof(AmCommand));
                    if ($$ == NULL) {
                        YYABORT;
                    }
                    $$->line = getLexerLineNumber();
                    $$->name = strdup($1);
                    if ($$->name == NULL) {
                        YYABORT;
                    }
                    $$->args = NULL;
                    CommandArgumentType argType = getCommandArgumentType(cmd);
                    if (argType == CMD_ARGS_BOOLEAN) {
                        setLexerArgumentType(AM_BOOLEAN_ARGS);
                    } else {
                        setLexerArgumentType(AM_WORD_ARGS);
                    }
                    $$->cmd = cmd;
                }
        ;

line_ending :
            TOK_EOL
        |   TOK_EOF
        ;

arguments : boolean_expression
                {
                    $$ = (AmCommandArguments *)malloc(
                            sizeof(AmCommandArguments));
                    if ($$ == NULL) {
                        YYABORT;
                    }
                    $$->booleanArgs = true;
                    $$->u.b = $1;
                }
        |   word_list
                {
                    /* Convert the builder list into an array.
                     * Do it in reverse order; the words were pushed
                     * onto the list in LIFO order.
                     */
                    AmWordList *w = (AmWordList *)malloc(sizeof(AmWordList));
                    if (w == NULL) {
                        YYABORT;
                    }
                    if ($1 != NULL) {
                        AmWordListBuilder *words = $1;

                        w->argc = words->wordCount;
                        w->argv = (const char **)malloc(w->argc *
                                        sizeof(char *));
                        if (w->argv == NULL) {
                            YYABORT;
                        }
                        int i;
                        for (i = w->argc; words != NULL && i > 0; --i) {
                            AmWordListBuilder *f = words;
                            w->argv[i-1] = words->word;
                            words = words->next;
                            free(f);
                        }
                        assert(i == 0);
                        assert(words == NULL);
                    } else {
                        w->argc = 0;
                        w->argv = NULL;
                    }
                    $$ = (AmCommandArguments *)malloc(
                            sizeof(AmCommandArguments));
                    if ($$ == NULL) {
                        YYABORT;
                    }
                    $$->booleanArgs = false;
                    $$->u.w = w;
                }
        ;

word_list : /* empty */
                { $$ = NULL; }
        |   word_list TOK_WORD
                {
                    if ($1 == NULL) {
                        $$ = (AmWordListBuilder *)malloc(
                                sizeof(AmWordListBuilder));
                        if ($$ == NULL) {
                            YYABORT;
                        }
                        $$->next = NULL;
                        $$->wordCount = 1;
                    } else {
                        $$ = (AmWordListBuilder *)malloc(
                                sizeof(AmWordListBuilder));
                        if ($$ == NULL) {
                            YYABORT;
                        }
                        $$->next = $1;
                        $$->wordCount = $$->next->wordCount + 1;
                    }
                    $$->word = strdup($2);
                    if ($$->word == NULL) {
                        YYABORT;
                    }
                }
        ;

boolean_expression :
            '!' boolean_expression
                {
                    $$ = (AmBooleanValue *)malloc(sizeof(AmBooleanValue));
                    if ($$ == NULL) {
                        YYABORT;
                    }
                    $$->type = AM_BVAL_EXPRESSION;
                    $$->u.expression.op = AM_BOP_NOT;
                    $$->u.expression.arg1 = $2;
                    $$->u.expression.arg2 = NULL;
                }
    /* TODO: if both expressions are literals, evaluate now */
        |   boolean_expression TOK_AND boolean_expression
                { BOOLEAN_EXPRESSION($$, $1, AM_BOP_AND, $3); }
        |   boolean_expression TOK_OR boolean_expression
                { BOOLEAN_EXPRESSION($$, $1, AM_BOP_OR, $3); }
        |   boolean_expression TOK_EQ boolean_expression
                { BOOLEAN_EXPRESSION($$, $1, AM_BOP_EQ, $3); }
        |   boolean_expression TOK_NE boolean_expression
                { BOOLEAN_EXPRESSION($$, $1, AM_BOP_NE, $3); }
        |   '(' boolean_expression ')'
                { $$ = $2; }
    /* TODO: if both strings are literals, evaluate now */
        |   string_value '<' string_value
                { STRING_COMPARISON($$, $1, AM_SOP_LT, $3); }
        |   string_value '>' string_value
                { STRING_COMPARISON($$, $1, AM_SOP_GT, $3); }
        |   string_value TOK_EQ string_value
                { STRING_COMPARISON($$, $1, AM_SOP_EQ, $3); }
        |   string_value TOK_NE string_value
                { STRING_COMPARISON($$, $1, AM_SOP_NE, $3); }
        |   string_value TOK_LE string_value
                { STRING_COMPARISON($$, $1, AM_SOP_LE, $3); }
        |   string_value TOK_GE string_value
                { STRING_COMPARISON($$, $1, AM_SOP_GE, $3); }
        ;

string_value :
            TOK_IDENTIFIER
                {
                    $$ = (AmStringValue *)malloc(sizeof(AmStringValue));
                    if ($$ == NULL) {
                        YYABORT;
                    }
                    $$->type = AM_SVAL_LITERAL;
                    $$->u.literal = strdup($1);
                    if ($$->u.literal == NULL) {
                        YYABORT;
                    }
                }
        |   TOK_STRING
                {
                    $$ = (AmStringValue *)malloc(sizeof(AmStringValue));
                    if ($$ == NULL) {
                        YYABORT;
                    }
                    $$->type = AM_SVAL_LITERAL;
                    $$->u.literal = strdup($1);
                    if ($$->u.literal == NULL) {
                        YYABORT;
                    }
                }
        |   function_call
                {
                    $$ = (AmStringValue *)malloc(sizeof(AmStringValue));
                    if ($$ == NULL) {
                        YYABORT;
                    }
                    $$->type = AM_SVAL_FUNCTION;
                    $$->u.function = $1;
                }
        ;

        /* We can't just say
         *  TOK_IDENTIFIER '(' function_arguments_or_empty ')'
         * because parsing function_arguments_or_empty will clobber
         * the underlying string that yylval.literalString points to.
         */
function_call :
            function_name '(' function_arguments_or_empty ')'
                {
                    Function *fn = findFunction($1);
                    if (fn == NULL) {
                        fprintf(stderr, "Unknown function \"%s\"\n", $1);
                        YYABORT;
                    }
                    $$ = (AmFunctionCall *)malloc(sizeof(AmFunctionCall));
                    if ($$ == NULL) {
                        YYABORT;
                    }
                    $$->name = $1;
                    if ($$->name == NULL) {
                        YYABORT;
                    }
                    $$->fn = fn;
                    $$->args = $3;
                }
        ;

function_name :
            TOK_IDENTIFIER
                {
                    $$ = strdup($1);
                }
        ;

function_arguments_or_empty :
            /* empty */
                {
                    $$ = (AmFunctionArguments *)malloc(
                            sizeof(AmFunctionArguments));
                    if ($$ == NULL) {
                        YYABORT;
                    }
                    $$->argc = 0;
                    $$->argv = NULL;
                }
        |   function_arguments
                {
                    AmFunctionArgumentBuilder *args = $1;
                    assert(args != NULL);

                    /* Convert the builder list into an array.
                     * Do it in reverse order; the args were pushed
                     * onto the list in LIFO order.
                     */
                    $$ = (AmFunctionArguments *)malloc(
                            sizeof(AmFunctionArguments));
                    if ($$ == NULL) {
                        YYABORT;
                    }
                    $$->argc = args->argCount;
                    $$->argv = (AmStringValue *)malloc(
                            $$->argc * sizeof(AmStringValue));
                    if ($$->argv == NULL) {
                        YYABORT;
                    }
                    int i;
                    for (i = $$->argc; args != NULL && i > 0; --i) {
                        AmFunctionArgumentBuilder *f = args;
                        $$->argv[i-1] = *args->arg;
                        args = args->next;
                        free(f->arg);
                        free(f);
                    }
                    assert(i == 0);
                    assert(args == NULL);
                }
        ;

function_arguments :
            string_value
                {
                    $$ = (AmFunctionArgumentBuilder *)malloc(
                            sizeof(AmFunctionArgumentBuilder));
                    if ($$ == NULL) {
                        YYABORT;
                    }
                    $$->next = NULL;
                    $$->argCount = 1;
                    $$->arg = $1;
                }
        |   function_arguments ',' string_value
                {
                    $$ = (AmFunctionArgumentBuilder *)malloc(
                            sizeof(AmFunctionArgumentBuilder));
                    if ($$ == NULL) {
                        YYABORT;
                    }
                    $$->next = $1;
                    $$->argCount = $$->next->argCount + 1;
                    $$->arg = $3;
                }
        ;
    /* xxx this whole tool needs to be hardened */
