/*
 * Copyright (c) 2013 Sergey 'Jin' Bostandzhyan
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

/* This is a hack for Rockchip rk3x based devices. The problem is that
 * the MEMERASE ioctl is failing (hangs and never returns) in their kernel.
 * The sources are not fully available, so fixing it in the rk30xxnand_ko driver
 * is not possible.
 *
 * I straced the stock recovery application and it seems to avoid this
 * particular ioctl, instead it is simply writing zeroes using the write() call.
 *
 * This workaround does the same and will replace all MEMERASE occurances in
 * the recovery code.
 */

#ifndef __RK3X_HACK_H__
#define __RK3X_HACK_H__

#include <sys/types.h>  // for size_t, etc.

// write zeroes to fd at position pos
int zero_out(int fd, off_t pos, ssize_t length);

#endif//__RK3X_HACK_H__
