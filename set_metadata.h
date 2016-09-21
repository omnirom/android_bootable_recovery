/*
 * Copyright (C) 2014 The Team Win Recovery Project
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

/*
 * The purpose of these functions is to try to get and set the proper
 * file permissions, SELinux contexts, owner, and group so that these
 * files are accessible when we boot up to normal Android via MTP and to
 * file manager apps. During early boot we try to read the contexts and
 * owner / group info from /data/media or from /data/media/0 and store
 * them in static variables. From there, we'll try to set the same
 * contexts, owner, and group information on most files we create during
 * operations like backups, copying the log, and MTP operations.
 */

#ifndef _RECOVERY_SET_CONTEXTS_H
#define _RECOVERY_SET_CONTEXTS_H

#include <sys/stat.h>
#include "selinux/selinux.h"

int tw_get_default_metadata(const char* filename);
int tw_set_default_metadata(const char* filename);

#endif //_RECOVERY_SET_CONTEXTS_H
