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

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <linux/input.h>

#include "bootloader.h"
#include "common.h"
#include "extra-functions.h"
#include "data.h"
#include "variables.h"

void run_script(const char *str1, const char *str2, const char *str3, const char *str4, const char *str5, const char *str6, const char *str7, int request_confirm)
{
	ui_print("%s", str1);
	ui_print("%s", str2);
	pid_t pid = fork();
	if (pid == 0) {
		char *args[] = { "/sbin/sh", "-c", (char*)str3, "1>&2", NULL };
		execv("/sbin/sh", args);
		fprintf(stderr, str4, strerror(errno));
		_exit(-1);
	}
	int status;
	while (waitpid(pid, &status, WNOHANG) == 0) {
		ui_print(".");
		sleep(1);
	}
	ui_print("\n");
	if (!WIFEXITED(status) || (WEXITSTATUS(status) != 0)) {
		ui_print("%s", str5);
	} else {
		ui_print("%s", str6);
	}
}

