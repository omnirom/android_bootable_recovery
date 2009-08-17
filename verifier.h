/*
 * Copyright (C) 2008 The Android Open Source Project
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

#ifndef _RECOVERY_VERIFIER_H
#define _RECOVERY_VERIFIER_H

#include "mincrypt/rsa.h"

/* Look in the file for a signature footer, and verify that it
 * matches one of the given keys.  Return one of the constants below.
 */
int verify_file(const char* path, const RSAPublicKey *pKeys, unsigned int numKeys);

#define VERIFY_SUCCESS        0
#define VERIFY_FAILURE        1

#endif  /* _RECOVERY_VERIFIER_H */
