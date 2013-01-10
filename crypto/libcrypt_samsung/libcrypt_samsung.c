/*
 * Copyright (c) 2013 a3955269 all rights reversed, no rights reserved.
 */

//////////////////////////////////////////////////////////////////////////////

#include <string.h>
#include <stdio.h>
#include <dlfcn.h>

#include "include/libcrypt_samsung.h"

//////////////////////////////////////////////////////////////////////////////

int decrypt_EDK(
        dek_t *dek, const edk_payload_t *edk, /*const*/ char *passwd)
{
    void *lib = dlopen("libsec_km.so", RTLD_LAZY);

    if(!lib)
        return -100;

    int r = -101;
    decrypt_EDK_t sym = (decrypt_EDK_t)dlsym(lib, "decrypt_EDK");
    if(sym)
        r = sym(dek, edk, passwd);

    dlclose(lib);

    return r;
}

int mount_ecryptfs_drive(
        const char *passwd, const char *source, const char *target, int filter)
{
    void *lib = dlopen("libsec_ecryptfs.so", RTLD_LAZY);
    if(!lib)
        return -100;

    int r = -101;
    mount_ecryptfs_drive_t sym = (mount_ecryptfs_drive_t)dlsym(lib, "mount_ecryptfs_drive");
    if(sym)
        r = sym(passwd, source, target, filter);

    dlclose(lib);

    return r;
}

int unmount_ecryptfs_drive(
        const char *source)
{
    void *lib = dlopen("libsec_ecryptfs.so", RTLD_LAZY);
    if(!lib)
        return -100;

    int r = -101;
    unmount_ecryptfs_drive_t sym = (unmount_ecryptfs_drive_t)dlsym(lib, "unmount_ecryptfs_drive");
    if(sym)
        r = sym(source);

    dlclose(lib);

    return r;
}