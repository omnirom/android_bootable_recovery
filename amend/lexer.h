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

#ifndef AMEND_LEXER_H_
#define AMEND_LEXER_H_

#define AMEND_LEXER_BUFFER_INPUT 1

void yyerror(const char *msg);
int yylex(void);

#if AMEND_LEXER_BUFFER_INPUT
void setLexerInputBuffer(const char *buf, size_t buflen);
#else
#include <stdio.h>
void yyset_in(FILE *in_str);
#endif

const char *tokenToString(int token);

typedef enum {
    AM_UNKNOWN_ARGS,
    AM_WORD_ARGS,
    AM_BOOLEAN_ARGS,
} AmArgumentType;

void setLexerArgumentType(AmArgumentType type);
int getLexerLineNumber(void);

#endif  // AMEND_LEXER_H_
