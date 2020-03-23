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

#include <memory>
#include <string>
#include <vector>

#include <android-base/macros.h>

#include "edify/expr.h"
#include "yydefs.h"
#include "parser.h"

extern int gLine;
extern int gColumn;

void yyerror(std::unique_ptr<Expr>* root, int* error_count, const char* s);
int yyparse(std::unique_ptr<Expr>* root, int* error_count);

struct yy_buffer_state;
void yy_switch_to_buffer(struct yy_buffer_state* new_buffer);
struct yy_buffer_state* yy_scan_string(const char* yystr);

// Convenience function for building expressions with a fixed number
// of arguments.
static Expr* Build(Function fn, YYLTYPE loc, size_t count, ...) {
    va_list v;
    va_start(v, count);
    Expr* e = new Expr(fn, "(operator)", loc.start, loc.end);
    for (size_t i = 0; i < count; ++i) {
        e->argv.emplace_back(va_arg(v, Expr*));
    }
    va_end(v);
    return e;
}

%}

%locations

%union {
    char* str;
    Expr* expr;
    std::vector<std::unique_ptr<Expr>>* args;
}

%token AND OR SUBSTR SUPERSTR EQ NE IF THEN ELSE ENDIF
%token <str> STRING BAD
%type <expr> expr
%type <args> arglist

%destructor { delete $$; } expr
%destructor { delete $$; } arglist

%parse-param {std::unique_ptr<Expr>* root}
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

input:  expr           { root->reset($1); }
;

expr:  STRING {
    $$ = new Expr(Literal, $1, @$.start, @$.end);
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
    Function fn = FindFunction($1);
    if (fn == nullptr) {
        std::string msg = "unknown function \"" + std::string($1) + "\"";
        yyerror(root, error_count, msg.c_str());
        YYERROR;
    }
    $$ = new Expr(fn, $1, @$.start, @$.end);
    $$->argv = std::move(*$3);
}
;

arglist:    /* empty */ {
    $$ = new std::vector<std::unique_ptr<Expr>>;
}
| expr {
    $$ = new std::vector<std::unique_ptr<Expr>>;
    $$->emplace_back($1);
}
| arglist ',' expr {
    UNUSED($1);
    $$->push_back(std::unique_ptr<Expr>($3));
}
;

%%

void yyerror(std::unique_ptr<Expr>* root, int* error_count, const char* s) {
  if (strlen(s) == 0) {
    s = "syntax error";
  }
  printf("line %d col %d: %s\n", gLine, gColumn, s);
  ++*error_count;
}

int ParseString(const std::string& str, std::unique_ptr<Expr>* root, int* error_count) {
  yy_switch_to_buffer(yy_scan_string(str.c_str()));
  return yyparse(root, error_count);
}
