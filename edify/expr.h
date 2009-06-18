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

#ifndef _EXPRESSION_H
#define _EXPRESSION_H

#include "yydefs.h"

#define MAX_STRING_LEN 1024

typedef struct Expr Expr;

typedef struct {
    // Optional pointer to app-specific data; the core of edify never
    // uses this value.
    void* cookie;

    // The source of the original script.  Must be NULL-terminated,
    // and in writable memory (Evaluate may make temporary changes to
    // it but will restore it when done).
    char* script;

    // The error message (if any) returned if the evaluation aborts.
    // Should be NULL initially, will be either NULL or a malloc'd
    // pointer after Evaluate() returns.
    char* errmsg;
} State;

typedef char* (*Function)(const char* name, State* state,
                          int argc, Expr* argv[]);

struct Expr {
    Function fn;
    char* name;
    int argc;
    Expr** argv;
    int start, end;
};

char* Evaluate(State* state, Expr* expr);

// Glue to make an Expr out of a literal.
char* Literal(const char* name, State* state, int argc, Expr* argv[]);

// Functions corresponding to various syntactic sugar operators.
// ("concat" is also available as a builtin function, to concatenate
// more than two strings.)
char* ConcatFn(const char* name, State* state, int argc, Expr* argv[]);
char* LogicalAndFn(const char* name, State* state, int argc, Expr* argv[]);
char* LogicalOrFn(const char* name, State* state, int argc, Expr* argv[]);
char* LogicalNotFn(const char* name, State* state, int argc, Expr* argv[]);
char* SubstringFn(const char* name, State* state, int argc, Expr* argv[]);
char* EqualityFn(const char* name, State* state, int argc, Expr* argv[]);
char* InequalityFn(const char* name, State* state, int argc, Expr* argv[]);
char* SequenceFn(const char* name, State* state, int argc, Expr* argv[]);

// Convenience function for building expressions with a fixed number
// of arguments.
Expr* Build(Function fn, YYLTYPE loc, int count, ...);

// Global builtins, registered by RegisterBuiltins().
char* IfElseFn(const char* name, State* state, int argc, Expr* argv[]);
char* AssertFn(const char* name, State* state, int argc, Expr* argv[]);
char* AbortFn(const char* name, State* state, int argc, Expr* argv[]);


// For setting and getting the global error string (when returning
// NULL from a function).
void SetError(const char* message);  // makes a copy
const char* GetError();              // retains ownership
void ClearError();


typedef struct {
  const char* name;
  Function fn;
} NamedFunction;

// Register a new function.  The same Function may be registered under
// multiple names, but a given name should only be used once.
void RegisterFunction(const char* name, Function fn);

// Register all the builtins.
void RegisterBuiltins();

// Call this after all calls to RegisterFunction() but before parsing
// any scripts to finish building the function table.
void FinishRegistration();

// Find the Function for a given name; return NULL if no such function
// exists.
Function FindFunction(const char* name);


// --- convenience functions for use in functions ---

// Evaluate the expressions in argv, giving 'count' char* (the ... is
// zero or more char** to put them in).  If any expression evaluates
// to NULL, free the rest and return -1.  Return 0 on success.
int ReadArgs(State* state, Expr* argv[], int count, ...);

// Evaluate the expressions in argv, returning an array of char*
// results.  If any evaluate to NULL, free the rest and return NULL.
// The caller is responsible for freeing the returned array and the
// strings it contains.
char** ReadVarArgs(State* state, int argc, Expr* argv[]);

// Use printf-style arguments to compose an error message to put into
// *state.  Returns NULL.
char* ErrorAbort(State* state, char* format, ...);


#endif  // _EXPRESSION_H
