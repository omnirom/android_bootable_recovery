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
#include <stdio.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
#include "permissions.h"

static int
test_permission_list()
{
    PermissionRequestList list;
    int ret;
    int numRequests;

    /* Bad parameter
     */
    ret = initPermissionRequestList(NULL);
    assert(ret < 0);

    /* Good parameter
     */
    ret = initPermissionRequestList(&list);
    assert(ret == 0);

    /* Bad parameters
     */
    ret = addPermissionRequestToList(NULL, NULL, false, 0);
    assert(ret < 0);

    ret = addPermissionRequestToList(&list, NULL, false, 0);
    assert(ret < 0);

    /* Good parameters
     */
    numRequests = 0;

    ret = addPermissionRequestToList(&list, "one", false, 1);
    assert(ret == 0);
    numRequests++;

    ret = addPermissionRequestToList(&list, "two", false, 2);
    assert(ret == 0);
    numRequests++;

    ret = addPermissionRequestToList(&list, "three", false, 3);
    assert(ret == 0);
    numRequests++;

    ret = addPermissionRequestToList(&list, "recursive", true, 55);
    assert(ret == 0);
    numRequests++;

    /* Validate the list
     */
    assert(list.requests != NULL);
    assert(list.numRequests == numRequests);
    assert(list.numRequests <= list.requestsAllocated);
    bool sawOne = false;
    bool sawTwo = false;
    bool sawThree = false;
    bool sawRecursive = false;
    int i;
    for (i = 0; i < list.numRequests; i++) {
        PermissionRequest *req = &list.requests[i];
        assert(req->allowed == 0);

        /* Order isn't guaranteed, so we have to switch every time.
         */
        if (strcmp(req->path, "one") == 0) {
            assert(!sawOne);
            assert(req->requested == 1);
            assert(!req->recursive);
            sawOne = true;
        } else if (strcmp(req->path, "two") == 0) {
            assert(!sawTwo);
            assert(req->requested == 2);
            assert(!req->recursive);
            sawTwo = true;
        } else if (strcmp(req->path, "three") == 0) {
            assert(!sawThree);
            assert(req->requested == 3);
            assert(!req->recursive);
            sawThree = true;
        } else if (strcmp(req->path, "recursive") == 0) {
            assert(!sawRecursive);
            assert(req->requested == 55);
            assert(req->recursive);
            sawRecursive = true;
        } else {
            assert(false);
        }
    }
    assert(sawOne);
    assert(sawTwo);
    assert(sawThree);
    assert(sawRecursive);

    /* Smoke test the teardown
     */
    freePermissionRequestListElements(&list);

    return 0;
}

static int
test_permission_table()
{
    int ret;

    /* Test the global permissions table.
     * Try calling functions without initializing first.
     */
    ret = registerPermissionSet(0, NULL);
    assert(ret < 0);

    ret = countPermissionConflicts((PermissionRequestList *)16, false);
    assert(ret < 0);

    ret = getPermissionCount();
    assert(ret < 0);

    const Permission *p;
    p = getPermissionAt(0);
    assert(p == NULL);

    /* Initialize.
     */
    ret = permissionInit();
    assert(ret == 0);

    /* Make sure we can't initialize twice.
     */
    ret = permissionInit();
    assert(ret < 0);

    /* Test the inspection functions.
     */
    ret = getPermissionCount();
    assert(ret == 0);

    p = getPermissionAt(-1);
    assert(p == NULL);

    p = getPermissionAt(0);
    assert(p == NULL);

    p = getPermissionAt(1);
    assert(p == NULL);

    /* Test registerPermissionSet().
     * Try some bad parameter values.
     */
    ret = registerPermissionSet(-1, NULL);
    assert(ret < 0);

    ret = registerPermissionSet(1, NULL);
    assert(ret < 0);

    /* Register some permissions.
     */
    Permission p1;
    p1.path = "one";
    p1.allowed = 1;
    ret = registerPermissionSet(1, &p1);
    assert(ret == 0);
    ret = getPermissionCount();
    assert(ret == 1);

    Permission p2[2];
    p2[0].path = "two";
    p2[0].allowed = 2;
    p2[1].path = "three";
    p2[1].allowed = 3;
    ret = registerPermissionSet(2, p2);
    assert(ret == 0);
    ret = getPermissionCount();
    assert(ret == 3);

    ret = registerPermissionSet(0, NULL);
    assert(ret == 0);
    ret = getPermissionCount();
    assert(ret == 3);

    p1.path = "four";
    p1.allowed = 4;
    ret = registerPermissionSet(1, &p1);
    assert(ret == 0);

    /* Make sure the table looks correct.
     * Order is important;  more-recent additions
     * should appear at higher indices.
     */
    ret = getPermissionCount();
    assert(ret == 4);

    int i;
    for (i = 0; i < ret; i++) {
        const Permission *p;
        p = getPermissionAt(i);
        assert(p != NULL);
        assert(p->allowed == (unsigned int)(i + 1));
        switch (i) {
        case 0:
            assert(strcmp(p->path, "one") == 0);
            break;
        case 1:
            assert(strcmp(p->path, "two") == 0);
            break;
        case 2:
            assert(strcmp(p->path, "three") == 0);
            break;
        case 3:
            assert(strcmp(p->path, "four") == 0);
            break;
        default:
            assert(!"internal error");
            break;
        }
    }
    p = getPermissionAt(ret);
    assert(p == NULL);

    /* Smoke test the teardown
     */
    permissionCleanup();

    return 0;
}

static int
test_allowed_permissions()
{
    int ret;
    int numPerms;

    /* Make sure these fail before initialization.
     */
    ret = countPermissionConflicts((PermissionRequestList *)1, false);
    assert(ret < 0);

    ret = getAllowedPermissions((const char *)1, false, (unsigned int *)1);
    assert(ret < 0);

    /* Initialize.
     */
    ret = permissionInit();
    assert(ret == 0);

    /* Make sure countPermissionConflicts() fails with bad parameters.
     */
    ret = countPermissionConflicts(NULL, false);
    assert(ret < 0);

    /* Register a set of permissions.
     */
    Permission perms[] = {
        { "/", PERM_NONE },
        { "/stat", PERM_STAT },
        { "/read", PERMSET_READ },
        { "/write", PERMSET_WRITE },
        { "/.stat", PERM_STAT },
        { "/.stat/.read", PERMSET_READ },
        { "/.stat/.read/.write", PERMSET_WRITE },
        { "/.stat/.write", PERMSET_WRITE },
    };
    numPerms = sizeof(perms) / sizeof(perms[0]);
    ret = registerPermissionSet(numPerms, perms);
    assert(ret == 0);

    /* Build a permission request list.
     */
    PermissionRequestList list;
    ret = initPermissionRequestList(&list);
    assert(ret == 0);

    ret = addPermissionRequestToList(&list, "/stat", false, PERM_STAT);
    assert(ret == 0);

    ret = addPermissionRequestToList(&list, "/read", false, PERM_READ);
    assert(ret == 0);

    ret = addPermissionRequestToList(&list, "/write", false, PERM_WRITE);
    assert(ret == 0);

    //TODO: cover more cases once the permission stuff has been implemented

    /* All of the requests in the list should be allowed.
     */
    ret = countPermissionConflicts(&list, false);
    assert(ret == 0);

    /* Add a request that will be denied.
     */
    ret = addPermissionRequestToList(&list, "/stat", false, 1<<31 | PERM_STAT);
    assert(ret == 0);

    ret = countPermissionConflicts(&list, false);
    assert(ret == 1);

    //TODO: more tests

    permissionCleanup();

    return 0;
}

int
test_permissions()
{
    int ret;

    ret = test_permission_list();
    if (ret != 0) {
        fprintf(stderr, "test_permission_list() failed: %d\n", ret);
        return ret;
    }

    ret = test_permission_table();
    if (ret != 0) {
        fprintf(stderr, "test_permission_table() failed: %d\n", ret);
        return ret;
    }

    ret = test_allowed_permissions();
    if (ret != 0) {
        fprintf(stderr, "test_permission_table() failed: %d\n", ret);
        return ret;
    }

    return 0;
}
