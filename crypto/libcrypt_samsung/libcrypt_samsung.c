/*
 * Copyright (c) 2013 a3955269 all rights reversed, no rights reserved.
 */

//////////////////////////////////////////////////////////////////////////////

#include <string.h>
#include <stdio.h>
#include <dlfcn.h>

#include "include/libcrypt_samsung.h"

//////////////////////////////////////////////////////////////////////////////
void xconvert_key_to_hex_ascii(unsigned char *master_key, unsigned int keysize,
                              char *master_key_ascii)
{
  unsigned int i, a;
  unsigned char nibble;

  for (i=0, a=0; i<keysize; i++, a+=2) {
    /* For each byte, write out two ascii hex digits */
    nibble = (master_key[i] >> 4) & 0xf;
    master_key_ascii[a] = nibble + (nibble > 9 ? 0x37 : 0x30);

    nibble = master_key[i] & 0xf;
    master_key_ascii[a+1] = nibble + (nibble > 9 ? 0x37 : 0x30);
  }

  /* Add the null termination */
  master_key_ascii[a] = '\0';

}

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

