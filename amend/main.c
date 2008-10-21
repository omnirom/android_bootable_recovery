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
#include "ast.h"
#include "lexer.h"
#include "parser.h"
#include "register.h"
#include "execute.h"

void
lexTest()
{
    int token;
    do {
        token = yylex();
        if (token == 0) {
            printf(" EOF");
            fflush(stdout);
            break;
        } else {
            printf(" %s", tokenToString(token));
            fflush(stdout);
            if (token == TOK_IDENTIFIER) {
                if (strcmp(yylval.literalString, "assert") == 0) {
                    setLexerArgumentType(AM_BOOLEAN_ARGS);
                } else {
                    setLexerArgumentType(AM_WORD_ARGS);
                }
                do {
                    token = yylex();
                    printf(" %s", tokenToString(token));
                    fflush(stdout);
                } while (token != TOK_EOL && token != TOK_EOF && token != 0);
            } else if (token != TOK_EOL) {
                fprintf(stderr, "syntax error: expected identifier\n");
                break;
            }
        }
    } while (token != 0);
    printf("\n");
}

void
usage()
{
    printf("usage: amend [--debug-lex|--debug-ast] [<filename>]\n");
    exit(1);
}

extern const AmCommandList *gCommands;
int
main(int argc, char *argv[])
{
    FILE *inputFile = NULL;
    bool debugLex = false;
    bool debugAst = false;
    const char *fileName = NULL;
    int err;

#if 1
    extern int test_symtab(void);
    int ret = test_symtab();
    if (ret != 0) {
        fprintf(stderr, "test_symtab() failed: %d\n", ret);
        exit(ret);
    }
    extern int test_cmd_fn(void);
    ret = test_cmd_fn();
    if (ret != 0) {
        fprintf(stderr, "test_cmd_fn() failed: %d\n", ret);
        exit(ret);
    }
    extern int test_permissions(void);
    ret = test_permissions();
    if (ret != 0) {
        fprintf(stderr, "test_permissions() failed: %d\n", ret);
        exit(ret);
    }
#endif

    argc--;
    argv++;
    while (argc > 0) {
        if (strcmp("--debug-lex", argv[0]) == 0) {
            debugLex = true;
        } else if (strcmp("--debug-ast", argv[0]) == 0) {
            debugAst = true;
        } else if (argv[0][0] == '-') {
            fprintf(stderr, "amend: Unknown option \"%s\"\n", argv[0]);
            usage();
        } else {
            fileName = argv[0];
        }
        argc--;
        argv++;
    }

    if (fileName != NULL) {
        inputFile = fopen(fileName, "r");
        if (inputFile == NULL) {
            fprintf(stderr, "amend: Can't open input file '%s'\n", fileName);
            usage();
        }
    }

    commandInit();
//xxx clean up

    err = registerUpdateCommands();
    if (err < 0) {
        fprintf(stderr, "amend: Error registering commands: %d\n", err);
        exit(-err);
    }
    err = registerUpdateFunctions();
    if (err < 0) {
        fprintf(stderr, "amend: Error registering functions: %d\n", err);
        exit(-err);
    }

#if AMEND_LEXER_BUFFER_INPUT
    if (inputFile == NULL) {
        fprintf(stderr, "amend: No input file\n");
        usage();
    }
    char *fileData;
    int fileDataLen;
    fseek(inputFile, 0, SEEK_END);
    fileDataLen = ftell(inputFile);
    rewind(inputFile);
    if (fileDataLen < 0) {
        fprintf(stderr, "amend: Can't get file length\n");
        exit(2);
    } else if (fileDataLen == 0) {
        printf("amend: Empty input file\n");
        exit(0);
    }
    fileData = (char *)malloc(fileDataLen + 1);
    if (fileData == NULL) {
        fprintf(stderr, "amend: Can't allocate %d bytes\n", fileDataLen + 1);
        exit(2);
    }
    size_t nread = fread(fileData, 1, fileDataLen, inputFile);
    if (nread != (size_t)fileDataLen) {
        fprintf(stderr, "amend: Didn't read %d bytes, only %zd\n", fileDataLen,
                nread);
        exit(2);
    }
    fileData[fileDataLen] = '\0';
    setLexerInputBuffer(fileData, fileDataLen);
#else
    if (inputFile == NULL) {
        inputFile = stdin;
    }
    yyset_in(inputFile);
#endif

    if (debugLex) {
        lexTest();
    } else {
        int ret = yyparse();
        if (ret != 0) {
            fprintf(stderr, "amend: Parse failed (%d)\n", ret);
            exit(2);
        } else {
            if (debugAst) {
                dumpCommandList(gCommands);
            }
printf("amend: Parse successful.\n");
            ret = execCommandList((ExecContext *)1, gCommands);
            if (ret != 0) {
                fprintf(stderr, "amend: Execution failed (%d)\n", ret);
                exit(3);
            }
printf("amend: Execution successful.\n");
        }
    }

    return 0;
}
