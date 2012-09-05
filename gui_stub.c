/*
 * Copyright (C) 2007 The Android Open Source Project
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

#include <linux/input.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

int gui_init()
{
    return -1;
}

int gui_loadResources()
{
    return -1;
}

int gui_start()
{
    return -1;
}

void gui_print(const char *fmt, ...)
{
    return;
}

void gui_print_overwrite(const char *fmt, ...)
{
    return;
}

void gui_notifyVarChange(const char *name, const char* value)
{
    return;
}

void gui_console_only(void)
{
	return;
}