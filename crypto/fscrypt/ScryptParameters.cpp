/*
 * Copyright (C) 2016 The Android Open Source Project
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

#include "ScryptParameters.h"

#include <stdlib.h>
#include <string.h>

bool parse_scrypt_parameters(const char* paramstr, int *Nf, int *rf, int *pf) {
    int params[3];
    char *token;
    char *saveptr;
    int i;

    /*
     * The token we're looking for should be three integers separated by
     * colons (e.g., "12:8:1"). Scan the property to make sure it matches.
     */
    for (i = 0, token = strtok_r(const_cast<char *>(paramstr), ":", &saveptr);
            token != nullptr && i < 3;
            i++, token = strtok_r(nullptr, ":", &saveptr)) {
        char *endptr;
        params[i] = strtol(token, &endptr, 10);

        /*
         * Check that there was a valid number and it's 8-bit.
         */
        if ((*token == '\0') || (*endptr != '\0') || params[i] < 0 || params[i] > 255) {
            return false;
        }
    }
    if (token != nullptr) {
        return false;
    }
    *Nf = params[0]; *rf = params[1]; *pf = params[2];
    return true;
}
