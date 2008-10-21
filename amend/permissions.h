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

#ifndef AMEND_PERMISSIONS_H_
#define AMEND_PERMISSIONS_H_

#include <stdbool.h>

#define PERM_NONE   (0)
#define PERM_STAT   (1<<0)
#define PERM_READ   (1<<1)
#define PERM_WRITE  (1<<2)  // including create, delete, mkdir, rmdir
#define PERM_CHMOD  (1<<3)
#define PERM_CHOWN  (1<<4)
#define PERM_CHGRP  (1<<5)
#define PERM_SETUID (1<<6)
#define PERM_SETGID (1<<7)

#define PERMSET_READ (PERM_STAT | PERM_READ)
#define PERMSET_WRITE (PERMSET_READ | PERM_WRITE)

#define PERMSET_ALL \
    (PERM_STAT | PERM_READ | PERM_WRITE | PERM_CHMOD | \
    PERM_CHOWN | PERM_CHGRP | PERM_SETUID | PERM_SETGID)

typedef struct {
    unsigned int requested;
    unsigned int allowed;
    const char *path;
    bool recursive;
} PermissionRequest;

typedef struct {
    PermissionRequest *requests;
    int numRequests;
    int requestsAllocated;
} PermissionRequestList;

/* Properly clear out a PermissionRequestList.
 *
 * @return 0 if list is non-NULL, negative otherwise.
 */
int initPermissionRequestList(PermissionRequestList *list);

/* Add a permission request to the list, allocating more space
 * if necessary.
 *
 * @return 0 on success or a negative value on failure.
 */
int addPermissionRequestToList(PermissionRequestList *list,
        const char *path, bool recursive, unsigned int permissions);

/* Free anything allocated by addPermissionRequestToList().  The caller
 * is responsible for freeing the actual PermissionRequestList.
 */
void freePermissionRequestListElements(PermissionRequestList *list);


/*
 * Global permission table
 */

typedef struct {
    const char *path;
    unsigned int allowed;
} Permission;

int permissionInit(void);
void permissionCleanup(void);

/* Returns the allowed permissions for the path in "outAllowed".
 * Returns 0 if successful, negative if a parameter or global state
 * is bad.
 */
int getAllowedPermissions(const char *path, bool recursive,
        unsigned int *outAllowed);

/* More-recently-registered permissions override older permissions.
 */
int registerPermissionSet(int count, Permission *set);

/* Check to make sure that each request is allowed.
 *
 * @param requests The list of permission requests
 * @param updateAllowed If true, update the "allowed" field in each
 *                      element of the list
 * @return the number of requests that were denied, or negative if
 *         an error occurred.
 */
int countPermissionConflicts(PermissionRequestList *requests,
        bool updateAllowed);

/* Inspection/testing/debugging functions
 */
int getPermissionCount(void);
const Permission *getPermissionAt(int index);

#endif  // AMEND_PERMISSIONS_H_
