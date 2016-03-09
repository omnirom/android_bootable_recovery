/*
 * Copyright (c) 2013 a3955269 all rights reversed, no rights reserved.
 * jcadduono may have had his fingers in here at some point.
 */

#include <string.h>
#include <stdio.h>
#include <dlfcn.h>

#include "libcrypt_samsung.h"

#define EDK_LIB "libsec_km.so"
#define ECRYPTFS_LIB "libsec_ecryptfs.so"

#define EDK_DECRYPT_SYM "decrypt_EDK"
#define ECRYPTFS_MOUNT_SYM "mount_ecryptfs_drive"
#define ECRYPTFS_UNMOUNT_SYM "unmount_ecryptfs_drive"

int decrypt_EDK(
        dek_t *dek, const edk_payload_t *edk, char *passwd)
{
    int rc = -1;

    void *lib = dlopen(EDK_LIB, RTLD_LAZY);
    if (!lib) {
        printf("Failed to open library '%s'\n", EDK_LIB);
        return rc;
    }

    decrypt_EDK_t sym = (decrypt_EDK_t) dlsym(lib, EDK_DECRYPT_SYM);
    if (!sym) {
        printf("Failed to find symbol '%s'\n", EDK_DECRYPT_SYM);
        goto close;
    }

    rc = sym(dek, edk, passwd);
    if (rc < 0)
        printf("Failed to decrypt EDK (rc: %d)\n", rc);

close:
    dlclose(lib);
    return rc;
}

int mount_ecryptfs_drive(
        const char *passwd, const char *source, const char *target, int filter)
{
    int rc = -1;

    void *lib = dlopen(ECRYPTFS_LIB, RTLD_LAZY);
    if (!lib) {
        printf("Failed to open library '%s'\n", ECRYPTFS_LIB);
        return rc;
    }

    mount_ecryptfs_drive_t sym = (mount_ecryptfs_drive_t) dlsym(lib, ECRYPTFS_MOUNT_SYM);
    if (!sym) {
        printf("Failed to find symbol '%s'\n", ECRYPTFS_MOUNT_SYM);
        goto close;
    }

    rc = sym(passwd, source, target, filter);
    if (rc < 0)
        printf("Failed to mount eCryptfs on %s (rc: %d)\n", target, rc);

close:
    dlclose(lib);
    return rc;
}

int unmount_ecryptfs_drive(
        const char *source)
{
    int rc = -1;

    void *lib = dlopen(ECRYPTFS_LIB, RTLD_LAZY);
    if (!lib) {
        printf("Failed to open library '%s'\n", ECRYPTFS_LIB);
        return rc;
    }

    unmount_ecryptfs_drive_t sym = (unmount_ecryptfs_drive_t) dlsym(lib, ECRYPTFS_UNMOUNT_SYM);
    if (!sym) {
        printf("Failed to find symbol '%s'\n", ECRYPTFS_UNMOUNT_SYM);
        goto close;
    }

    rc = sym(source);
    if (rc < 0)
        printf("Failed to unmount eCryptfs %s (rc: %d)\n", source, rc);

close:
    dlclose(lib);
    return rc;
}
