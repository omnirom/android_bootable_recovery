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

#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "expr.h"

// Functions should:
//
//    - return a malloc()'d string
//    - if Evaluate() on any argument returns NULL, return NULL.

int BooleanString(const char* s) {
  return s[0] != '\0';
}

char* Evaluate(void* cookie, Expr* expr) {
  return expr->fn(expr->name, cookie, expr->argc, expr->argv);
}

char* ConcatFn(const char* name, void* cookie, int argc, Expr* argv[]) {
  if (argc == 0) {
    return strdup("");
  }
  char** strings = malloc(argc * sizeof(char*));
  int i;
  for (i = 0; i < argc; ++i) {
    strings[i] = NULL;
  }
  char* result = NULL;
  int length = 0;
  for (i = 0; i < argc; ++i) {
    strings[i] = Evaluate(cookie, argv[i]);
    if (strings[i] == NULL) {
      goto done;
    }
    length += strlen(strings[i]);
  }

  result = malloc(length+1);
  int p = 0;
  for (i = 0; i < argc; ++i) {
    strcpy(result+p, strings[i]);
    p += strlen(strings[i]);
  }
  result[p] = '\0';

done:
  for (i = 0; i < argc; ++i) {
    free(strings[i]);
  }
  return result;
}

char* IfElseFn(const char* name, void* cookie, int argc, Expr* argv[]) {
  if (argc != 2 && argc != 3) {
    return NULL;
  }
  char* cond = Evaluate(cookie, argv[0]);
  if (cond == NULL) {
    return NULL;
  }

  if (BooleanString(cond) == true) {
    free(cond);
    return Evaluate(cookie, argv[1]);
  } else {
    if (argc == 3) {
      free(cond);
      return Evaluate(cookie, argv[2]);
    } else {
      return cond;
    }
  }
}

char* AbortFn(const char* name, void* cookie, int argc, Expr* argv[]) {
  return NULL;
}

char* AssertFn(const char* name, void* cookie, int argc, Expr* argv[]) {
  int i;
  for (i = 0; i < argc; ++i) {
    char* v = Evaluate(cookie, argv[i]);
    if (v == NULL) {
      return NULL;
    }
    int b = BooleanString(v);
    free(v);
    if (!b) {
      return NULL;
    }
  }
  return strdup("");
}

char* PrintFn(const char* name, void* cookie, int argc, Expr* argv[]) {
  int i;
  for (i = 0; i < argc; ++i) {
    char* v = Evaluate(cookie, argv[i]);
    if (v == NULL) {
      return NULL;
    }
    fputs(v, stdout);
    free(v);
  }
  return strdup("");
}

char* LogicalAndFn(const char* name, void* cookie,
                   int argc, Expr* argv[]) {
  char* left = Evaluate(cookie, argv[0]);
  if (left == NULL) return NULL;
  if (BooleanString(left) == true) {
    free(left);
    return Evaluate(cookie, argv[1]);
  } else {
    return left;
  }
}

char* LogicalOrFn(const char* name, void* cookie,
                  int argc, Expr* argv[]) {
  char* left = Evaluate(cookie, argv[0]);
  if (left == NULL) return NULL;
  if (BooleanString(left) == false) {
    free(left);
    return Evaluate(cookie, argv[1]);
  } else {
    return left;
  }
}

char* LogicalNotFn(const char* name, void* cookie,
                  int argc, Expr* argv[]) {
  char* val = Evaluate(cookie, argv[0]);
  if (val == NULL) return NULL;
  bool bv = BooleanString(val);
  free(val);
  if (bv) {
    return strdup("");
  } else {
    return strdup("t");
  }
}

char* SubstringFn(const char* name, void* cookie,
                  int argc, Expr* argv[]) {
  char* needle = Evaluate(cookie, argv[0]);
  if (needle == NULL) return NULL;
  char* haystack = Evaluate(cookie, argv[1]);
  if (haystack == NULL) {
    free(needle);
    return NULL;
  }

  char* result = strdup(strstr(haystack, needle) ? "t" : "");
  free(needle);
  free(haystack);
  return result;
}

char* EqualityFn(const char* name, void* cookie, int argc, Expr* argv[]) {
  char* left = Evaluate(cookie, argv[0]);
  if (left == NULL) return NULL;
  char* right = Evaluate(cookie, argv[1]);
  if (right == NULL) {
    free(left);
    return NULL;
  }

  char* result = strdup(strcmp(left, right) == 0 ? "t" : "");
  free(left);
  free(right);
  return result;
}

char* InequalityFn(const char* name, void* cookie, int argc, Expr* argv[]) {
  char* left = Evaluate(cookie, argv[0]);
  if (left == NULL) return NULL;
  char* right = Evaluate(cookie, argv[1]);
  if (right == NULL) {
    free(left);
    return NULL;
  }

  char* result = strdup(strcmp(left, right) != 0 ? "t" : "");
  free(left);
  free(right);
  return result;
}

char* SequenceFn(const char* name, void* cookie, int argc, Expr* argv[]) {
  char* left = Evaluate(cookie, argv[0]);
  if (left == NULL) return NULL;
  free(left);
  return Evaluate(cookie, argv[1]);
}

char* Literal(const char* name, void* cookie, int argc, Expr* argv[]) {
  return strdup(name);
}

Expr* Build(Function fn, int count, ...) {
  va_list v;
  va_start(v, count);
  Expr* e = malloc(sizeof(Expr));
  e->fn = fn;
  e->name = "(operator)";
  e->argc = count;
  e->argv = malloc(count * sizeof(Expr*));
  int i;
  for (i = 0; i < count; ++i) {
    e->argv[i] = va_arg(v, Expr*);
  }
  va_end(v);
  return e;
}

static int fn_entries = 0;
static int fn_size = 0;
NamedFunction* fn_table = NULL;

void RegisterFunction(const char* name, Function fn) {
  if (fn_entries <= fn_size) {
    fn_size = fn_size*2 + 1;
    fn_table = realloc(fn_table, fn_size * sizeof(NamedFunction));
  }
  fn_table[fn_entries].name = name;
  fn_table[fn_entries].fn = fn;
  ++fn_entries;
}

static int fn_entry_compare(const void* a, const void* b) {
  const char* na = ((const NamedFunction*)a)->name;
  const char* nb = ((const NamedFunction*)b)->name;
  return strcmp(na, nb);
}

void FinishRegistration() {
  qsort(fn_table, fn_entries, sizeof(NamedFunction), fn_entry_compare);
}

Function FindFunction(const char* name) {
  NamedFunction key;
  key.name = name;
  NamedFunction* nf = bsearch(&key, fn_table, fn_entries, sizeof(NamedFunction),
                              fn_entry_compare);
  if (nf == NULL) {
    return NULL;
  }
  return nf->fn;
}

void RegisterBuiltins() {
  RegisterFunction("ifelse", IfElseFn);
  RegisterFunction("abort", AbortFn);
  RegisterFunction("assert", AssertFn);
  RegisterFunction("concat", ConcatFn);
  RegisterFunction("is_substring", SubstringFn);
  RegisterFunction("print", PrintFn);
}
