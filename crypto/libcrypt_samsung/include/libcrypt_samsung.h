/*
 * Copyright (c) 2013 a3955269 all rights reversed, no rights reserved.
 */

#ifndef __LIBCRYPT_SAMSUNG_H__
#define __LIBCRYPT_SAMSUNG_H__

//////////////////////////////////////////////////////////////////////////////
// Name                           Address  Ordinal
// ----                           -------  -------
// SECKM_AES_set_encrypt_key      000010D8
// SECKM_AES_set_decrypt_key      00001464
// SECKM_AES_encrypt              00001600
// SECKM_AES_decrypt              00001A10
// SECKM_aes_selftest             00001D94
// verify_EDK                     00001F7C
// encrypt_dek                    00001FC8
// decrypt_EDK                    000020D4
// change_EDK                     0000218C
// generate_dek_salt              000022A4
// create_EDK                     000023A0
// free_DEK                       000024DC
// alloc_DEK                      000024F4
// SECKM_HMAC_SHA256              00002500
// SECKM_HMAC_SHA256_selftest     00002690
// pbkdf                          000026FC
// pbkdf_selftest                 00002898
// _SECKM_PRNG_get16              00002958
// SECKM_PRNG_get16               00002C48
// _SECKM_PRNG_init               00002C54
// SECKM_PRNG_selftest            00002F38
// SECKM_PRNG_set_seed            00002FF0
// SECKM_PRNG_init                00002FF8
// SECKM_SHA256_Transform         00003004
// SECKM_SHA256_Final             000031D8
// SECKM_SHA256_Update            00003330
// SECKM_SHA256_Init              000033FC
// SECKM_SHA2_selftest            00003430
// integrity_check                00003488
// update_system_property         00003580
// setsec_km_fips_status          00003630
// _all_checks                    00003684
// get_fips_status                000036D4


// EDK Payload is defined as:
//    Encrypted DEK – EDK itself
//    HMAC of EDK (32 bytes ???)
//    Salt         16 bytes

#define EDK_MAGIC   0x1001e4b1

#pragma pack(1)

typedef struct {
    unsigned int magic;     // EDK_MAGIC
    unsigned int flags;     // 2
    unsigned int zeros[6];
} dek_t;

typedef struct {
    unsigned char data[32];
} edk_t;


// size 0x70 -> 112
typedef struct {
    dek_t dek;
    edk_t edk;
    unsigned char hmac[32];
    unsigned char salt[16];
} edk_payload_t;

#pragma pack()

//////////////////////////////////////////////////////////////////////////////

int decrypt_EDK(
        dek_t *dek, const edk_payload_t *edk, /*const*/ char *passwd);

typedef int (*decrypt_EDK_t)(
        dek_t *dek, const edk_payload_t *edk, /*const*/ char *passwd);


int verify_EDK(const edk_payload_t *edk, const char *passwd);
//change_EDK()
//create_EDK()

// internally just mallocs 32 bytes
dek_t *alloc_DEK();
void free_DEK(dek_t *dek);
//encrypt_dek()
//generate_dek_salt()

//pbkdf(_buf_, "passwordPASSWORDpassword", 0x18, "saltSALTsaltSALTsaltSALTsaltSALTsalt", 0x24, 0x1000, 0x140);
int pbkdf(
        void *buf, void *pw, int pwlen, void *salt, int saltlen, int hashcnt,
        int keylen);

// getprop("rw.km_fips_status")
// "ready, undefined, error_selftest, error_integrity"
int get_fips_status();

//////////////////////////////////////////////////////////////////////////////
//
// libsec_ecryptfs.so (internally uses libkeyutils.so)
//
// Name                   Address  Ordinal
// ----                   -------  -------
// unmount_ecryptfs_drive 00000A78
// mount_ecryptfs_drive   00000B48
// fips_read_edk          00000E44
// fips_save_edk          00000EA4
// fips_create_edk        00000F20
// fips_change_password   00001018
// fips_delete_edk        00001124
//

// might depend on /data beeing mounted for reading /data/system/edk_p_sd
//
// filter
// 0: building options without file encryption filtering.
// 1: building options with media files filtering.
// 2: building options with all new files filtering.

int mount_ecryptfs_drive(
        const char *passwd, const char *source, const char *target, int filter);

typedef int (*mount_ecryptfs_drive_t)(
        const char *passwd, const char *source, const char *target, int filter);

// calls 2 times umount2(source, MNT_EXPIRE)
int unmount_ecryptfs_drive(
        const char *source);

typedef int (*unmount_ecryptfs_drive_t)(
        const char *source);

//////////////////////////////////////////////////////////////////////////////

#endif // #ifndef __LIBCRYPT_SAMSUNG_H__

//////////////////////////////////////////////////////////////////////////////

