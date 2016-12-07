/*
 * Copyright (C) 2016 The Team Win Recovery Project
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

#include "Decrypt.h"
#include "Ext4Crypt.h"

#include <string>

#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "ext4_crypt.h"
#include "key_control.h"

#include <hardware/gatekeeper.h>
#include "HashPassword.h"

#include <android-base/file.h>

int gatekeeper_device_initialize(gatekeeper_device_t **dev) {
	int ret;
	const hw_module_t *mod;
	ret = hw_get_module_by_class(GATEKEEPER_HARDWARE_MODULE_ID, NULL, &mod);

	if (ret!=0) {
		printf("failed to get hw module\n");
		return ret;
	}

	ret = gatekeeper_open(mod, dev);

	if (ret!=0)
		printf("failed to open gatekeeper\n");
	return ret;
}

int Get_Password_Type(const userid_t user_id, std::string& filename) {
	std::string path;
    if (user_id == 0) {
		path = "/data/system/";
	} else {
		char user_id_str[5];
		sprintf(user_id_str, "%i", user_id);
		path = "/data/system/users/";
		path += user_id_str;
		path += "/";
	}
	filename = path + "gatekeeper.password.key";
	struct stat st;
	if (stat(filename.c_str(), &st) == 0 && st.st_size > 0)
		return 1;
	filename = path + "gatekeeper.pattern.key";
	if (stat(filename.c_str(), &st) == 0 && st.st_size > 0)
		return 2;
	printf("Unable to locate gatekeeper password file '%s'\n", filename.c_str());
	filename = "";
	return 0;
}

bool Decrypt_DE() {
	if (!e4crypt_initialize_global_de()) { // this deals with the overarching device encryption
		printf("e4crypt_initialize_global_de returned fail\n");
		return false;
	}
	if (!e4crypt_init_user0()) {
		printf("e4crypt_init_user0 returned fail\n");
		return false;
	}
	return true;
}

bool Decrypt_User(const userid_t user_id, const std::string& Password) {
    uint8_t *auth_token;
    uint32_t auth_token_len;
    int ret;

    struct stat st;
    if (user_id > 9999) {
		printf("user_id is too big\n");
		return false;
	}
    std::string filename;
    bool Default_Password = (Password == "!");
    if (Get_Password_Type(user_id, filename) == 0 && !Default_Password) {
		printf("Unknown password type\n");
		return false;
	}
    int flags = FLAG_STORAGE_DE;
    if (user_id == 0)
		flags = FLAG_STORAGE_DE;
	else
		flags = FLAG_STORAGE_CE;
	gatekeeper_device_t *device;
	ret = gatekeeper_device_initialize(&device);
	if (Default_Password) {
		if (!e4crypt_unlock_user_key(user_id, 0, "!", "!")) {
			printf("e4crypt_unlock_user_key returned fail\n");
			return false;
		}
		if (!e4crypt_prepare_user_storage(nullptr, user_id, 0, flags)) {
			printf("failed to e4crypt_prepare_user_storage\n");
			return false;
		}
		printf("Decrypted Successfully!\n");
		return true;
	}
    if (ret!=0)
		return false;
	printf("password filename is '%s'\n", filename.c_str());
	if (stat(filename.c_str(), &st) != 0) {
		printf("error stat'ing key file: %s\n", strerror(errno));
		return false;
	}
	std::string handle;
    if (!android::base::ReadFileToString(filename, &handle)) {
		printf("Failed to read '%s'\n", filename.c_str());
		return false;
	}
    bool should_reenroll;
    ret = device->verify(device, user_id, 0, (const uint8_t *)handle.c_str(), st.st_size,
                (const uint8_t *)Password.c_str(), (uint32_t)Password.size(), &auth_token, &auth_token_len,
                &should_reenroll);
    if (ret !=0) {
		printf("failed to verify\n");
		return false;
	}
	char token_hex[(auth_token_len*2)+1];
	token_hex[(auth_token_len*2)] = 0;
	uint32_t i;
	for (i=0;i<auth_token_len;i++) {
		sprintf(&token_hex[2*i], "%02X", auth_token[i]);
	}
	// The secret is "Android FBE credential hash" plus appended 0x00 to reach 128 bytes then append the user's password then feed that to sha512sum
	std::string secret = HashPassword(Password);
	if (!e4crypt_unlock_user_key(user_id, 0, token_hex, secret.c_str())) {
		printf("e4crypt_unlock_user_key returned fail\n");
		return false;
	}
	if (!e4crypt_prepare_user_storage(nullptr, user_id, 0, flags)) {
		printf("failed to e4crypt_prepare_user_storage\n");
		return false;
	}
	printf("Decrypted Successfully!\n");
	return true;
}
