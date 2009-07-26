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
#include "parser.h"

extern int yyparse(Expr** root, int* error_count);

int expect(const char* expr_str, const char* expected, int* errors) {
    Expr* e;
    int error;
    char* result;

    printf(".");

    yy_scan_string(expr_str);
    int error_count = 0;
    error = yyparse(&e, &error_count);
    if (error > 0 || error_count > 0) {
        fprintf(stderr, "error parsing \"%s\" (%d errors)\n",
                expr_str, error_count);
        ++*errors;
        return 0;
    }

    State state;
    state.cookie = NULL;
    state.script = expr_str;
    state.errmsg = NULL;

    result = Evaluate(&state, e);
    free(state.errmsg);
    if (result == NULL && expected != NULL) {
        fprintf(stderr, "error evaluating \"%s\"\n", expr_str);
        ++*errors;
        return 0;
    }

    if (result == NULL && expected == NULL) {
        return 1;
    }

    if (strcmp(result, expected) != 0) {
        fprintf(stderr, "evaluating \"%s\": expected \"%s\", got \"%s\"\n",
                expr_str, expected, result);
        ++*errors;
        free(result);
        return 0;
    }

    free(result);
    return 1;
}

int test() {
    int errors = 0;

    expect("a", "a", &errors);
    expect("\"a\"", "a", &errors);
    expect("\"\\x61\"", "a", &errors);
    expect("# this is a comment\n"
           "  a\n"
           "   \n",
           "a", &errors);


    // sequence operator
    expect("a; b; c", "c", &errors);

    // string concat operator
    expect("a + b", "ab", &errors);
    expect("a + \n \"b\"", "ab", &errors);
    expect("a + b +\nc\n", "abc", &errors);

    // string concat function
    expect("concat(a, b)", "ab", &errors);
    expect("concat(a,\n \"b\")", "ab", &errors);
    expect("concat(a + b,\nc,\"d\")", "abcd", &errors);
    expect("\"concat\"(a + b,\nc,\"d\")", "abcd", &errors);

    // logical and
    expect("a && b", "b", &errors);
    expect("a && \"\"", "", &errors);
    expect("\"\" && b", "", &errors);
    expect("\"\" && \"\"", "", &errors);
    expect("\"\" && abort()", "", &errors);   // test short-circuiting
    expect("t && abort()", NULL, &errors);

    // logical or
    expect("a || b", "a", &errors);
    expect("a || \"\"", "a", &errors);
    expect("\"\" || b", "b", &errors);
    expect("\"\" || \"\"", "", &errors);
    expect("a || abort()", "a", &errors);     // test short-circuiting
    expect("\"\" || abort()", NULL, &errors);

    // logical not
    expect("!a", "", &errors);
    expect("! \"\"", "t", &errors);
    expect("!!a", "t", &errors);

    // precedence
    expect("\"\" == \"\" && b", "b", &errors);
    expect("a + b == ab", "t", &errors);
    expect("ab == a + b", "t", &errors);
    expect("a + (b == ab)", "a", &errors);
    expect("(ab == a) + b", "b", &errors);

    // substring function
    expect("is_substring(cad, abracadabra)", "t", &errors);
    expect("is_substring(abrac, abracadabra)", "t", &errors);
    expect("is_substring(dabra, abracadabra)", "t", &errors);
    expect("is_substring(cad, abracxadabra)", "", &errors);
    expect("is_substring(abrac, axbracadabra)", "", &errors);
    expect("is_substring(dabra, abracadabrxa)", "", &errors);

    // ifelse function
    expect("ifelse(t, yes, no)", "yes", &errors);
    expect("ifelse(!t, yes, no)", "no", &errors);
    expect("ifelse(t, yes, abort())", "yes", &errors);
    expect("ifelse(!t, abort(), no)", "no", &errors);

    // if "statements"
    expect("if t then yes else no endif", "yes", &errors);
    expect("if \"\" then yes else no endif", "no", &errors);
    expect("if \"\" then yes endif", "", &errors);
    expect("if \"\"; t then yes endif", "yes", &errors);

    // numeric comparisons
    expect("less_than_int(3, 14)", "t", &errors);
    expect("less_than_int(14, 3)", "", &errors);
    expect("less_than_int(x, 3)", "", &errors);
    expect("less_than_int(3, x)", "", &errors);
    expect("greater_than_int(3, 14)", "", &errors);
    expect("greater_than_int(14, 3)", "t", &errors);
    expect("greater_than_int(x, 3)", "", &errors);
    expect("greater_than_int(3, x)", "", &errors);

    printf("\n");

    return errors;
}

void ExprDump(int depth, Expr* n, char* script) {
    printf("%*s", depth*2, "");
    char temp = script[n->end];
    script[n->end] = '\0';
    printf("%s %p (%d-%d) \"%s\"\n",
           n->name == NULL ? "(NULL)" : n->name, n->fn, n->start, n->end,
           script+n->start);
    script[n->end] = temp;
    int i;
    for (i = 0; i < n->argc; ++i) {
        ExprDump(depth+1, n->argv[i], script);
    }
}

int main(int argc, char** argv) {
    RegisterBuiltins();
    FinishRegistration();

    if (argc == 1) {
        return test() != 0;
    }

    FILE* f = fopen(argv[1], "r");
    char buffer[8192];
    int size = fread(buffer, 1, 8191, f);
    fclose(f);
    buffer[size] = '\0';

    Expr* root;
    int error_count = 0;
    yy_scan_bytes(buffer, size);
    int error = yyparse(&root, &error_count);
    printf("parse returned %d; %d errors encountered\n", error, error_count);
    if (error == 0 || error_count > 0) {

        ExprDump(0, root, buffer);

        State state;
        state.cookie = NULL;
        state.script = buffer;
        state.errmsg = NULL;

        char* result = Evaluate(&state, root);
        if (result == NULL) {
            printf("result was NULL, message is: %s\n",
                   (state.errmsg == NULL ? "(NULL)" : state.errmsg));
            free(state.errmsg);
        } else {
            printf("result is [%s]\n", result);
        }
    }
    return 0;
}
