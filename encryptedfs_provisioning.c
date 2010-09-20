/*
 * Copyright (C) 2009 The Android Open Source Project
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "encryptedfs_provisioning.h"
#include "cutils/misc.h"
#include "cutils/properties.h"
#include "common.h"
#include "mtdutils/mtdutils.h"
#include "mtdutils/mounts.h"
#include "roots.h"

const char* encrypted_fs_enabled_property      = "persist.security.secfs.enabled";
const char* encrypted_fs_property_dir          = "/data/property/";
const char* encrypted_fs_system_dir            = "/data/system/";
const char* encrypted_fs_key_file_name         = "/data/fs_key.dat";
const char* encrypted_fs_salt_file_name        = "/data/hash_salt.dat";
const char* encrypted_fs_hash_file_src_name    = "/data/system/password.key";
const char* encrypted_fs_hash_file_dst_name    = "/data/hash.dat";
const char* encrypted_fs_entropy_file_src_name = "/data/system/entropy.dat";
const char* encrypted_fs_entropy_file_dst_name = "/data/ported_entropy.dat";

void get_property_file_name(char *buffer, const char *property_name) {
    sprintf(buffer, "%s%s", encrypted_fs_property_dir, property_name);
}

int get_binary_file_contents(char *buffer, int buf_size, const char *file_name, int *out_size) {
    FILE *in_file;
    int read_bytes;

    in_file = fopen(file_name, "r");
    if (in_file == NULL) {
        LOGE("Secure FS: error accessing key file.");
        return ENCRYPTED_FS_ERROR;
    }

    read_bytes = fread(buffer, 1, buf_size, in_file);
    if (out_size == NULL) {
        if (read_bytes != buf_size) {
            // Error or unexpected data
            fclose(in_file);
            LOGE("Secure FS: error reading conmplete key.");
            return ENCRYPTED_FS_ERROR;
        }
    } else {
        *out_size = read_bytes;
    }
    fclose(in_file);
    return ENCRYPTED_FS_OK;
}

int set_binary_file_contents(char *buffer, int buf_size, const char *file_name) {
    FILE *out_file;
    int write_bytes;

    out_file = fopen(file_name, "w");
    if (out_file == NULL) {
        LOGE("Secure FS: error setting up key file.");
        return ENCRYPTED_FS_ERROR;
    }

    write_bytes = fwrite(buffer, 1, buf_size, out_file);
    if (write_bytes != buf_size) {
        // Error or unexpected data
        fclose(out_file);
        LOGE("Secure FS: error reading conmplete key.");
        return ENCRYPTED_FS_ERROR;
    }

    fclose(out_file);
    return ENCRYPTED_FS_OK;
}

int get_text_file_contents(char *buffer, int buf_size, char *file_name) {
    FILE *in_file;
    char *read_data;

    in_file = fopen(file_name, "r");
    if (in_file == NULL) {
        LOGE("Secure FS: error accessing properties.");
        return ENCRYPTED_FS_ERROR;
    }

    read_data = fgets(buffer, buf_size, in_file);
    if (read_data == NULL) {
        // Error or unexpected data
        fclose(in_file);
        LOGE("Secure FS: error accessing properties.");
        return ENCRYPTED_FS_ERROR;
    }

    fclose(in_file);
    return ENCRYPTED_FS_OK;
}

int set_text_file_contents(char *buffer, char *file_name) {
    FILE *out_file;
    int result;

    out_file = fopen(file_name, "w");
    if (out_file == NULL) {
        LOGE("Secure FS: error setting up properties.");
        return ENCRYPTED_FS_ERROR;
    }

    result = fputs(buffer, out_file);
    if (result != 0) {
        // Error or unexpected data
        fclose(out_file);
        LOGE("Secure FS: error setting up properties.");
        return ENCRYPTED_FS_ERROR;
    }

    fflush(out_file);
    fclose(out_file);
    return ENCRYPTED_FS_OK;
}

int read_encrypted_fs_boolean_property(const char *prop_name, int *value) {
    char prop_file_name[PROPERTY_KEY_MAX + 32];
    char prop_value[PROPERTY_VALUE_MAX];
    int result;

    get_property_file_name(prop_file_name, prop_name);
    result = get_text_file_contents(prop_value, PROPERTY_VALUE_MAX, prop_file_name);

    if (result < 0) {
        return result;
    }

    if (strncmp(prop_value, "1", 1) == 0) {
        *value = 1;
    } else if (strncmp(prop_value, "0", 1) == 0) {
        *value = 0;
    } else {
        LOGE("Secure FS: error accessing properties.");
        return ENCRYPTED_FS_ERROR;
    }

    return ENCRYPTED_FS_OK;
}

int write_encrypted_fs_boolean_property(const char *prop_name, int value) {
    char prop_file_name[PROPERTY_KEY_MAX + 32];
    char prop_value[PROPERTY_VALUE_MAX];
    int result;

    get_property_file_name(prop_file_name, prop_name);

    // Create the directory if needed
    mkdir(encrypted_fs_property_dir, 0755);
    if (value == 1) {
        result = set_text_file_contents("1", prop_file_name);
    } else if (value == 0) {
        result = set_text_file_contents("0", prop_file_name);
    } else {
        return ENCRYPTED_FS_ERROR;
    }
    if (result < 0) {
        return result;
    }

    return ENCRYPTED_FS_OK;
}

int read_encrypted_fs_info(encrypted_fs_info *encrypted_fs_data) {
    int result;
    int value;
    result = ensure_path_mounted("/data");
    if (result != 0) {
        LOGE("Secure FS: error mounting userdata partition.");
        return ENCRYPTED_FS_ERROR;
    }

    // Read the pre-generated encrypted FS key, password hash and salt.
    result = get_binary_file_contents(encrypted_fs_data->key, ENCRYPTED_FS_KEY_SIZE,
            encrypted_fs_key_file_name, NULL);
    if (result != 0) {
        LOGE("Secure FS: error reading generated file system key.");
        return ENCRYPTED_FS_ERROR;
    }

    result = get_binary_file_contents(encrypted_fs_data->salt, ENCRYPTED_FS_SALT_SIZE,
            encrypted_fs_salt_file_name, &(encrypted_fs_data->salt_length));
    if (result != 0) {
        LOGE("Secure FS: error reading file system salt.");
        return ENCRYPTED_FS_ERROR;
    }

    result = get_binary_file_contents(encrypted_fs_data->hash, ENCRYPTED_FS_MAX_HASH_SIZE,
            encrypted_fs_hash_file_src_name, &(encrypted_fs_data->hash_length));
    if (result != 0) {
        LOGE("Secure FS: error reading password hash.");
        return ENCRYPTED_FS_ERROR;
    }

    result = get_binary_file_contents(encrypted_fs_data->entropy, ENTROPY_MAX_SIZE,
            encrypted_fs_entropy_file_src_name, &(encrypted_fs_data->entropy_length));
    if (result != 0) {
        LOGE("Secure FS: error reading ported entropy.");
        return ENCRYPTED_FS_ERROR;
    }

    result = ensure_path_unmounted("/data");
    if (result != 0) {
        LOGE("Secure FS: error unmounting data partition.");
        return ENCRYPTED_FS_ERROR;
    }

    return ENCRYPTED_FS_OK;
}

int restore_encrypted_fs_info(encrypted_fs_info *encrypted_fs_data) {
    int result;
    result = ensure_path_mounted("/data");
    if (result != 0) {
        LOGE("Secure FS: error mounting userdata partition.");
        return ENCRYPTED_FS_ERROR;
    }

    // Write the pre-generated secure FS key, password hash and salt.
    result = set_binary_file_contents(encrypted_fs_data->key, ENCRYPTED_FS_KEY_SIZE,
            encrypted_fs_key_file_name);
    if (result != 0) {
        LOGE("Secure FS: error writing generated file system key.");
        return ENCRYPTED_FS_ERROR;
    }

    result = set_binary_file_contents(encrypted_fs_data->salt, encrypted_fs_data->salt_length,
        encrypted_fs_salt_file_name);
    if (result != 0) {
        LOGE("Secure FS: error writing file system salt.");
        return ENCRYPTED_FS_ERROR;
    }

    result = set_binary_file_contents(encrypted_fs_data->hash, encrypted_fs_data->hash_length,
            encrypted_fs_hash_file_dst_name);
    if (result != 0) {
        LOGE("Secure FS: error writing password hash.");
        return ENCRYPTED_FS_ERROR;
    }

    result = set_binary_file_contents(encrypted_fs_data->entropy, encrypted_fs_data->entropy_length,
            encrypted_fs_entropy_file_dst_name);
    if (result != 0) {
        LOGE("Secure FS: error writing ported entropy.");
        return ENCRYPTED_FS_ERROR;
    }

    // Set the secure FS properties to their respective values
    result = write_encrypted_fs_boolean_property(encrypted_fs_enabled_property, encrypted_fs_data->mode);
    if (result != 0) {
        return result;
    }

    result = ensure_path_unmounted("/data");
    if (result != 0) {
        LOGE("Secure FS: error unmounting data partition.");
        return ENCRYPTED_FS_ERROR;
    }

    return ENCRYPTED_FS_OK;
}
