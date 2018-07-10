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

#include <memory>
#include <string>
#include <vector>

// Forward declaration to avoid including "otautil/error_code.h".
enum ErrorCode : int;
enum CauseCode : int;

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
  ErrorCode error_code;

  // cause code provides more detailed reason of an OTA failure (e.g. fsync error)
  // in addition to the error code.
  CauseCode cause_code;

  bool is_retry = false;
};

struct Value {
  enum class Type {
    STRING = 1,
    BLOB = 2,
  };

  Value(Type type, const std::string& str) : type(type), data(str) {}

  Type type;
  std::string data;
};

struct Expr;

using Function = Value* (*)(const char* name, State* state,
                            const std::vector<std::unique_ptr<Expr>>& argv);

struct Expr {
  Function fn;
  std::string name;
  std::vector<std::unique_ptr<Expr>> argv;
  int start, end;

  Expr(Function fn, const std::string& name, int start, int end) :
    fn(fn),
    name(name),
    start(start),
    end(end) {}
};

// Evaluate the input expr, return the resulting Value.
Value* EvaluateValue(State* state, const std::unique_ptr<Expr>& expr);

// Evaluate the input expr, assert that it is a string, and update the result parameter. This
// function returns true if the evaluation succeeds. This is a convenience function for older
// functions that want to deal only with strings.
bool Evaluate(State* state, const std::unique_ptr<Expr>& expr, std::string* result);

// Glue to make an Expr out of a literal.
Value* Literal(const char* name, State* state, const std::vector<std::unique_ptr<Expr>>& argv);

// Functions corresponding to various syntactic sugar operators.
// ("concat" is also available as a builtin function, to concatenate
// more than two strings.)
Value* ConcatFn(const char* name, State* state, const std::vector<std::unique_ptr<Expr>>& argv);
Value* LogicalAndFn(const char* name, State* state, const std::vector<std::unique_ptr<Expr>>& argv);
Value* LogicalOrFn(const char* name, State* state, const std::vector<std::unique_ptr<Expr>>& argv);
Value* LogicalNotFn(const char* name, State* state, const std::vector<std::unique_ptr<Expr>>& argv);
Value* SubstringFn(const char* name, State* state, const std::vector<std::unique_ptr<Expr>>& argv);
Value* EqualityFn(const char* name, State* state, const std::vector<std::unique_ptr<Expr>>& argv);
Value* InequalityFn(const char* name, State* state, const std::vector<std::unique_ptr<Expr>>& argv);
Value* SequenceFn(const char* name, State* state, const std::vector<std::unique_ptr<Expr>>& argv);

// Global builtins, registered by RegisterBuiltins().
Value* IfElseFn(const char* name, State* state, const std::vector<std::unique_ptr<Expr>>& argv);
Value* AssertFn(const char* name, State* state, const std::vector<std::unique_ptr<Expr>>& argv);
Value* AbortFn(const char* name, State* state, const std::vector<std::unique_ptr<Expr>>& argv);

// Register a new function.  The same Function may be registered under
// multiple names, but a given name should only be used once.
void RegisterFunction(const std::string& name, Function fn);

// Register all the builtins.
void RegisterBuiltins();

// Find the Function for a given name; return NULL if no such function
// exists.
Function FindFunction(const std::string& name);

// --- convenience functions for use in functions ---

// Evaluate the expressions in argv, and put the results of strings in args. If any expression
// evaluates to nullptr, return false. Return true on success.
bool ReadArgs(State* state, const std::vector<std::unique_ptr<Expr>>& argv,
              std::vector<std::string>* args);
bool ReadArgs(State* state, const std::vector<std::unique_ptr<Expr>>& argv,
              std::vector<std::string>* args, size_t start, size_t len);

// Evaluate the expressions in argv, and put the results of Value* in args. If any
// expression evaluate to nullptr, return false. Return true on success.
bool ReadValueArgs(State* state, const std::vector<std::unique_ptr<Expr>>& argv,
                   std::vector<std::unique_ptr<Value>>* args);
bool ReadValueArgs(State* state, const std::vector<std::unique_ptr<Expr>>& argv,
                   std::vector<std::unique_ptr<Value>>* args, size_t start, size_t len);

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

int ParseString(const std::string& str, std::unique_ptr<Expr>* root, int* error_count);

#endif  // _EXPRESSION_H
