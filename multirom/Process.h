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

#ifndef _PROCESS_H
#define _PROCESS_H

#ifdef __cplusplus

class Process {
public:
    static int killProcessesWithOpenFiles(const char *path, int signal);
    static int getPid(const char *s);
    static int checkSymLink(int pid, const char *path, const char *name);
    static int checkFileMaps(int pid, const char *path);
    static int checkFileMaps(int pid, const char *path, char *openFilename, size_t max);
    static int checkFileDescriptorSymLinks(int pid, const char *mountPoint);
    static int checkFileDescriptorSymLinks(int pid, const char *mountPoint, char *openFilename, size_t max);
    static void getProcessName(int pid, char *buffer, size_t max);
private:
    static int readSymLink(const char *path, char *link, size_t max);
    static int pathMatchesMountPoint(const char *path, const char *mountPoint);
};

extern "C" {
#endif /* __cplusplus */
	void vold_killProcessesWithOpenFiles(const char *path, int signal);
#ifdef __cplusplus
}
#endif

#endif
