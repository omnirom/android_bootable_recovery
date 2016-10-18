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

#include <unistd.h>
#include <string>

#include "error_code.h"

struct State {
    State(const std::string& script, void* cookie);

    // The source of the original script.
    const std::string& script;

    // Optional pointer to app-specific data; the core of edify never
    // uses this value.
    void* cookie;

    // The error message (if any) returned if the evaluation aborts.
    // Should be empty initially, will be either empty or a string that
    // Evaluate() returns.
    std::string errmsg;

    // error code indicates the type of failure (e.g. failure to update system image)
    // during the OTA process.
    ErrorCode error_code = kNoError;

    // cause code provides more detailed reason of an OTA failure (e.g. fsync error)
    // in addition to the error code.
    CauseCode cause_code = kNoCause;

    bool is_retry = false;
};

enum ValueType {
    VAL_INVALID = -1,
    VAL_STRING = 1,
    VAL_BLOB = 2,
};

struct Value {
    ValueType type;
    std::string data;

    Value(ValueType type, const std::string& str) :
        type(type),
        data(str) {}
};

struct Expr;

using Function = Value* (*)(const char* name, State* state, int argc, Expr* argv[]);

struct Expr {
    Function fn;
    const char* name;
    int argc;
    Expr** argv;
    int start, end;
};

// Take one of the Expr*s passed to the function as an argument,
// evaluate it, return the resulting Value.  The caller takes
// ownership of the returned Value.
Value* EvaluateValue(State* state, Expr* expr);

// Take one of the Expr*s passed to the function as an argument,
// evaluate it, assert that it is a string, and update the result
// parameter. This function returns true if the evaluation succeeds.
// This is a convenience function for older functions that want to
// deal only with strings.
bool Evaluate(State* state, Expr* expr, std::string* result);

// Glue to make an Expr out of a literal.
Value* Literal(const char* name, State* state, int argc, Expr* argv[]);

// Functions corresponding to various syntactic sugar operators.
// ("concat" is also available as a builtin function, to concatenate
// more than two strings.)
Value* ConcatFn(const char* name, State* state, int argc, Expr* argv[]);
Value* LogicalAndFn(const char* name, State* state, int argc, Expr* argv[]);
Value* LogicalOrFn(const char* name, State* state, int argc, Expr* argv[]);
Value* LogicalNotFn(const char* name, State* state, int argc, Expr* argv[]);
Value* SubstringFn(const char* name, State* state, int argc, Expr* argv[]);
Value* EqualityFn(const char* name, State* state, int argc, Expr* argv[]);
Value* InequalityFn(const char* name, State* state, int argc, Expr* argv[]);
Value* SequenceFn(const char* name, State* state, int argc, Expr* argv[]);

// Global builtins, registered by RegisterBuiltins().
Value* IfElseFn(const char* name, State* state, int argc, Expr* argv[]);
Value* AssertFn(const char* name, State* state, int argc, Expr* argv[]);
Value* AbortFn(const char* name, State* state, int argc, Expr* argv[]);

// Register a new function.  The same Function may be registered under
// multiple names, but a given name should only be used once.
void RegisterFunction(const std::string& name, Function fn);

// Register all the builtins.
void RegisterBuiltins();

// Find the Function for a given name; return NULL if no such function
// exists.
Function FindFunction(const std::string& name);

// --- convenience functions for use in functions ---

// Evaluate the expressions in argv, and put the results of strings in
// args. If any expression evaluates to nullptr, free the rest and return
// false. Return true on success.
bool ReadArgs(State* state, int argc, Expr* argv[], std::vector<std::string>* args);

// Evaluate the expressions in argv, and put the results of Value* in
// args. If any expression evaluate to nullptr, free the rest and return
// false. Return true on success.
bool ReadValueArgs(State* state, int argc, Expr* argv[], std::vector<std::unique_ptr<Value>>* args);

// Use printf-style arguments to compose an error message to put into
// *state.  Returns NULL.
Value* ErrorAbort(State* state, const char* format, ...)
    __attribute__((format(printf, 2, 3), deprecated));

// ErrorAbort has an optional (but recommended) argument 'cause_code'. If the cause code
// is set, it will be logged into last_install and provides reason of OTA failures.
Value* ErrorAbort(State* state, CauseCode cause_code, const char* format, ...)
    __attribute__((format(printf, 3, 4)));

// Copying the string into a Value.
Value* StringValue(const char* str);

Value* StringValue(const std::string& str);

int parse_string(const char* str, Expr** root, int* error_count);

#endif  // _EXPRESSION_H
