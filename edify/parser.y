%{
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "expr.h"
#include "yydefs.h"
#include "parser.h"

extern int gLine;
extern int gColumn;

void yyerror(Expr** root, int* error_count, const char* s);
int yyparse(Expr** root, int* error_count);

struct yy_buffer_state;
void yy_switch_to_buffer(struct yy_buffer_state* new_buffer);
struct yy_buffer_state* yy_scan_string(const char* yystr);

%}

%locations

%union {
    char* str;
    Expr* expr;
    struct {
        int argc;
        Expr** argv;
    } args;
}

%token AND OR SUBSTR SUPERSTR EQ NE IF THEN ELSE ENDIF
%token <str> STRING BAD
%type <expr> expr
%type <args> arglist

%parse-param {Expr** root}
%parse-param {int* error_count}
%error-verbose

/* declarations in increasing order of precedence */
%left ';'
%left ','
%left OR
%left AND
%left EQ NE
%left '+'
%right '!'

%%

input:  expr           { *root = $1; }
;

expr:  STRING {
    $$ = reinterpret_cast<Expr*>(malloc(sizeof(Expr)));
    $$->fn = Literal;
    $$->name = $1;
    $$->argc = 0;
    $$->argv = NULL;
    $$->start = @$.start;
    $$->end = @$.end;
}
|  '(' expr ')'                      { $$ = $2; $$->start=@$.start; $$->end=@$.end; }
|  expr ';'                          { $$ = $1; $$->start=@1.start; $$->end=@1.end; }
|  expr ';' expr                     { $$ = Build(SequenceFn, @$, 2, $1, $3); }
|  error ';' expr                    { $$ = $3; $$->start=@$.start; $$->end=@$.end; }
|  expr '+' expr                     { $$ = Build(ConcatFn, @$, 2, $1, $3); }
|  expr EQ expr                      { $$ = Build(EqualityFn, @$, 2, $1, $3); }
|  expr NE expr                      { $$ = Build(InequalityFn, @$, 2, $1, $3); }
|  expr AND expr                     { $$ = Build(LogicalAndFn, @$, 2, $1, $3); }
|  expr OR expr                      { $$ = Build(LogicalOrFn, @$, 2, $1, $3); }
|  '!' expr                          { $$ = Build(LogicalNotFn, @$, 1, $2); }
|  IF expr THEN expr ENDIF           { $$ = Build(IfElseFn, @$, 2, $2, $4); }
|  IF expr THEN expr ELSE expr ENDIF { $$ = Build(IfElseFn, @$, 3, $2, $4, $6); }
| STRING '(' arglist ')' {
    $$ = reinterpret_cast<Expr*>(malloc(sizeof(Expr)));
    $$->fn = FindFunction($1);
    if ($$->fn == NULL) {
        char buffer[256];
        snprintf(buffer, sizeof(buffer), "unknown function \"%s\"", $1);
        yyerror(root, error_count, buffer);
        YYERROR;
    }
    $$->name = $1;
    $$->argc = $3.argc;
    $$->argv = $3.argv;
    $$->start = @$.start;
    $$->end = @$.end;
}
;

arglist:    /* empty */ {
    $$.argc = 0;
    $$.argv = NULL;
}
| expr {
    $$.argc = 1;
    $$.argv = reinterpret_cast<Expr**>(malloc(sizeof(Expr*)));
    $$.argv[0] = $1;
}
| arglist ',' expr {
    $$.argc = $1.argc + 1;
    $$.argv = reinterpret_cast<Expr**>(realloc($$.argv, $$.argc * sizeof(Expr*)));
    $$.argv[$$.argc-1] = $3;
}
;

%%

void yyerror(Expr** root, int* error_count, const char* s) {
  if (strlen(s) == 0) {
    s = "syntax error";
  }
  printf("line %d col %d: %s\n", gLine, gColumn, s);
  ++*error_count;
}

int parse_string(const char* str, Expr** root, int* error_count) {
    yy_switch_to_buffer(yy_scan_string(str));
    return yyparse(root, error_count);
}
