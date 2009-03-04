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

#include <stdlib.h>
#include <string.h>
#include "permissions.h"

int
initPermissionRequestList(PermissionRequestList *list)
{
    if (list != NULL) {
        list->requests = NULL;
        list->numRequests = 0;
        list->requestsAllocated = 0;
        return 0;
    }
    return -1;
}

int
addPermissionRequestToList(PermissionRequestList *list,
        const char *path, bool recursive, unsigned int permissions)
{
    if (list == NULL || list->numRequests < 0 ||
            list->requestsAllocated < list->numRequests || path == NULL)
    {
        return -1;
    }

    if (list->numRequests == list->requestsAllocated) {
        int newSize;
        PermissionRequest *newRequests;

        newSize = list->requestsAllocated * 2;
        if (newSize < 16) {
            newSize = 16;
        }
        newRequests = (PermissionRequest *)realloc(list->requests,
                newSize * sizeof(PermissionRequest));
        if (newRequests == NULL) {
            return -2;
        }
        list->requests = newRequests;
        list->requestsAllocated = newSize;
    }

    PermissionRequest *req;
    req = &list->requests[list->numRequests++];
    req->path = strdup(path);
    if (req->path == NULL) {
        list->numRequests--;
        return -3;
    }
    req->recursive = recursive;
    req->requested = permissions;
    req->allowed = 0;

    return 0;
}

void
freePermissionRequestListElements(PermissionRequestList *list)
{
    if (list != NULL && list->numRequests >= 0 &&
            list->requestsAllocated >= list->numRequests)
    {
        int i;
        for (i = 0; i < list->numRequests; i++) {
            free((void *)list->requests[i].path);
        }
        free(list->requests);
        initPermissionRequestList(list);
    }
}

/*
 * Global permission table
 */

static struct {
    Permission *permissions;
    int numPermissionEntries;
    int allocatedPermissionEntries;
    bool permissionStateInitialized;
} gPermissionState = {
#if 1
    NULL, 0, 0, false
#else
    .permissions = NULL,
    .numPermissionEntries = 0,
    .allocatedPermissionEntries = 0,
    .permissionStateInitialized = false
#endif
};

int
permissionInit()
{
    if (gPermissionState.permissionStateInitialized) {
        return -1;
    }
    gPermissionState.permissions = NULL;
    gPermissionState.numPermissionEntries = 0;
    gPermissionState.allocatedPermissionEntries = 0;
    gPermissionState.permissionStateInitialized = true;
//xxx maybe add an "namespace root gets no permissions" fallback by default
    return 0;
}

void
permissionCleanup()
{
    if (gPermissionState.permissionStateInitialized) {
        gPermissionState.permissionStateInitialized = false;
        if (gPermissionState.permissions != NULL) {
            int i;
            for (i = 0; i < gPermissionState.numPermissionEntries; i++) {
                free((void *)gPermissionState.permissions[i].path);
            }
            free(gPermissionState.permissions);
        }
    }
}

int
getPermissionCount()
{
    if (gPermissionState.permissionStateInitialized) {
        return gPermissionState.numPermissionEntries;
    }
    return -1;
}

const Permission *
getPermissionAt(int index)
{
    if (!gPermissionState.permissionStateInitialized) {
        return NULL;
    }
    if (index < 0 || index >= gPermissionState.numPermissionEntries) {
        return NULL;
    }
    return &gPermissionState.permissions[index];
}

int
getAllowedPermissions(const char *path, bool recursive,
        unsigned int *outAllowed)
{
    if (!gPermissionState.permissionStateInitialized) {
        return -2;
    }
    if (outAllowed == NULL) {
        return -1;
    }
    *outAllowed = 0;
    if (path == NULL) {
        return -1;
    }
    //TODO: implement this for real.
    recursive = false;
    *outAllowed = PERMSET_ALL;
    return 0;
}

int
countPermissionConflicts(PermissionRequestList *requests, bool updateAllowed)
{
    if (!gPermissionState.permissionStateInitialized) {
        return -2;
    }
    if (requests == NULL || requests->requests == NULL ||
            requests->numRequests < 0 ||
            requests->requestsAllocated < requests->numRequests)
    {
        return -1;
    }
    int conflicts = 0;
    int i;
    for (i = 0; i < requests->numRequests; i++) {
        PermissionRequest *req;
        unsigned int allowed;
        int ret;

        req = &requests->requests[i];
        ret = getAllowedPermissions(req->path, req->recursive, &allowed);
        if (ret < 0) {
            return ret;
        }
        if ((req->requested & ~allowed) != 0) {
            conflicts++;
        }
        if (updateAllowed) {
            req->allowed = allowed;
        }
    }
    return conflicts;
}

int
registerPermissionSet(int count, Permission *set)
{
    if (!gPermissionState.permissionStateInitialized) {
        return -2;
    }
    if (count < 0 || (count > 0 && set == NULL)) {
        return -1;
    }
    if (count == 0) {
        return 0;
    }

    if (gPermissionState.numPermissionEntries + count >=
            gPermissionState.allocatedPermissionEntries)
    {
        Permission *newList;
        int newSize;

        newSize = (gPermissionState.allocatedPermissionEntries + count) * 2;
        if (newSize < 16) {
            newSize = 16;
        }
        newList = (Permission *)realloc(gPermissionState.permissions,
                newSize * sizeof(Permission));
        if (newList == NULL) {
            return -3;
        }
        gPermissionState.permissions = newList;
        gPermissionState.allocatedPermissionEntries = newSize;
    }

    Permission *p = &gPermissionState.permissions[
                        gPermissionState.numPermissionEntries];
    int i;
    for (i = 0; i < count; i++) {
        *p = set[i];
        //TODO: cache the strlen of the path
        //TODO: normalize; strip off trailing /
        p->path = strdup(p->path);
        if (p->path == NULL) {
            /* If we can't add all of the entries, we don't
             * add any of them.
             */
            Permission *pp = &gPermissionState.permissions[
                                gPermissionState.numPermissionEntries];
            while (pp != p) {
                free((void *)pp->path);
                pp++;
            }
            return -4;
        }
        p++;
    }
    gPermissionState.numPermissionEntries += count;

    return 0;
}
