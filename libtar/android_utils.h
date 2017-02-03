/*
** Copyright 2008, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#ifndef LIBTAR_ANDROID_UTILS_H
#define LIBTAR_ANDROID_UTILS_H

#define USER_INODE_SEPARATOR "\t"

char* scan_xattrs_for_user_inode (const char *realname);
int write_path_inode(const char* parent, const char* name, const char* inode_xattr);

#endif
