/*
 * Copyright (C) 2009 The Android Open Source Project
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

#ifndef _YYDEFS_H_
#define _YYDEFS_H_

#define YYLTYPE YYLTYPE
typedef struct {
    int start, end;
} YYLTYPE;

#define YYLLOC_DEFAULT(Current, Rhs, N) \
    do { \
        if (N) { \
            (Current).start = YYRHSLOC(Rhs, 1).start; \
            (Current).end = YYRHSLOC(Rhs, N).end; \
        } else { \
            (Current).start = YYRHSLOC(Rhs, 0).start; \
            (Current).end = YYRHSLOC(Rhs, 0).end; \
        } \
    } while (0)

int yylex();

#endif
