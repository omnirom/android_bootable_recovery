/*
 * Copyright (C) 2011 The Android Open Source Project
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

#ifndef __TRANSPORT_H
#define __TRANSPORT_H

/* convenience wrappers around read/write that will retry on
** EINTR and/or short read/write.  Returns 0 on success, -1
** on error or EOF.
*/
int readx(int fd, void *ptr, size_t len);
int writex(int fd, const void *ptr, size_t len);
#endif   /* __TRANSPORT_H */
