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

#ifndef ANDROID_VOLD_SCRYPT_PARAMETERS_H
#define ANDROID_VOLD_SCRYPT_PARAMETERS_H

#include <stdbool.h>
#include <sys/cdefs.h>

#define SCRYPT_PROP "ro.crypto.scrypt_params"
#define SCRYPT_DEFAULTS "15:3:1"

__BEGIN_DECLS

bool parse_scrypt_parameters(const char* paramstr, int *Nf, int *rf, int *pf);

__END_DECLS

#endif
