/* system/core/include/logwrap/logwrap.h
 *
 * Copyright 2013, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __LIBS_LOGWRAP_H
#define __LIBS_LOGWRAP_H

#include <stdbool.h>

__BEGIN_DECLS

/*
 * Run a command while logging its stdout and stderr
 *
 * WARNING: while this function is running it will clear all SIGCHLD handlers
 * if you rely on SIGCHLD in the caller there is a chance zombies will be
 * created if you're not calling waitpid after calling this. This function will
 * log a warning when it clears SIGCHLD for processes other than the child it
 * created.
 *
 * Arguments:
 *   argc:   the number of elements in argv
 *   argv:   an array of strings containing the command to be executed and its
 *           arguments as separate strings. argv does not need to be
 *           NULL-terminated
 *   status: the equivalent child status as populated by wait(status). This
 *           value is only valid when logwrap successfully completes. If NULL
 *           the return value of the child will be the function's return value.
 *   ignore_int_quit: set to true if you want to completely ignore SIGINT and
 *           SIGQUIT while logwrap is running. This may force the end-user to
 *           send a signal twice to signal the caller (once for the child, and
 *           once for the caller)
 *   log_target: Specify where to log the output of the child, either LOG_NONE,
 *           LOG_ALOG (for the Android system log), LOG_KLOG (for the kernel
 *           log), or LOG_FILE (and you need to specify a pathname in the
 *           file_path argument, otherwise pass NULL).  These are bit fields,
 *           and can be OR'ed together to log to multiple places.
 *   abbreviated: If true, capture up to the first 100 lines and last 4K of
 *           output from the child.  The abbreviated output is not dumped to
 *           the specified log until the child has exited.
 *   file_path: if log_target has the LOG_FILE bit set, then this parameter
 *           must be set to the pathname of the file to log to.
 *
 * Return value:
 *   0 when logwrap successfully run the child process and captured its status
 *   -1 when an internal error occurred
 *   -ECHILD if status is NULL and the child didn't exit properly
 *   the return value of the child if it exited properly and status is NULL
 *
 */

/* Values for the log_target parameter android_fork_execvp_ext() */
#define LOG_NONE        0
#define LOG_ALOG        1
#define LOG_KLOG        2
#define LOG_FILE        4

int android_fork_execvp_ext(int argc, char* argv[], int *status, bool ignore_int_quit,
        int log_target, bool abbreviated, char *file_path);

/* Similar to above, except abbreviated logging is not available, and if logwrap
 * is true, logging is to the Android system log, and if false, there is no
 * logging.
 */
static inline int android_fork_execvp(int argc, char* argv[], int *status,
                                     bool ignore_int_quit, bool logwrap)
{
    return android_fork_execvp_ext(argc, argv, status, ignore_int_quit,
                                   (logwrap ? LOG_ALOG : LOG_NONE), false, NULL);
}

__END_DECLS

#endif /* __LIBS_LOGWRAP_H */
