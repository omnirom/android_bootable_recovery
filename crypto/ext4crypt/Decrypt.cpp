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
#include "Weaver1.h"

#include <map>
#include <string>

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

#if 0
#include "ext4_crypt.h"
#include "key_control.h"
#else
#include <ext4_utils/ext4_crypt.h>
#include <keyutils.h>
#endif

#include <hardware/gatekeeper.h>
#include "HashPassword.h"

#include <android-base/file.h>

#include <openssl/sha.h>
#include <openssl/aes.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

// Store main DE raw ref / policy
extern std::string de_raw_ref;
extern std::map<userid_t, std::string> s_de_key_raw_refs;
extern std::map<userid_t, std::string> s_ce_key_raw_refs;

static bool lookup_ref_key_internal(std::map<userid_t, std::string>& key_map, const char* policy, userid_t* user_id) {
    for (std::map<userid_t, std::string>::iterator it=key_map.begin(); it!=key_map.end(); ++it) {
        if (strncmp(it->second.c_str(), policy, it->second.size()) == 0) {
            *user_id = it->first;
            return true;
        }
    }
    return false;
}

extern "C" bool lookup_ref_key(const char* policy, char* policy_type) {
    userid_t user_id = 0;
    if (strncmp(de_raw_ref.c_str(), policy, de_raw_ref.size()) == 0) {
        strcpy(policy_type, "1DK");
        return true;
    }
    if (!lookup_ref_key_internal(s_de_key_raw_refs, policy, &user_id)) {
        if (!lookup_ref_key_internal(s_ce_key_raw_refs, policy, &user_id)) {
            return false;
		} else
		    sprintf(policy_type, "1CE%d", user_id);
    } else
        sprintf(policy_type, "1DE%d", user_id);
    return true;
}

extern "C" bool lookup_ref_tar(const char* policy_type, char* policy) {
    if (strncmp(policy_type, "1", 1) != 0) {
        printf("Unexpected version %c\n", policy_type);
        return false;
    }
    const char* ptr = policy_type + 1; // skip past the version number
    if (strncmp(ptr, "DK", 2) == 0) {
        strncpy(policy, de_raw_ref.data(), de_raw_ref.size());
        return true;
    }
    userid_t user_id = atoi(ptr + 2);
    std::string raw_ref;
    if (*ptr == 'D') {
        if (lookup_key_ref(s_de_key_raw_refs, user_id, &raw_ref)) {
            strncpy(policy, raw_ref.data(), raw_ref.size());
        } else
            return false;
    } else if (*ptr == 'C') {
        if (lookup_key_ref(s_ce_key_raw_refs, user_id, &raw_ref)) {
            strncpy(policy, raw_ref.data(), raw_ref.size());
        } else
            return false;
    } else {
        printf("unknown policy type '%s'\n", policy_type);
        return false;
    }
    return true;
}

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

void output_hex(const std::string& in) {
	const char *buf = in.data();
	char hex[in.size() * 2 + 1];
	unsigned int index;
	for (index = 0; index < in.size(); index++)
		sprintf(&hex[2 * index], "%02X", buf[index]);
	printf("%s", hex);
}

void output_hex(const char* buf, const int size) {
	char hex[size * 2 + 1];
	unsigned int index;
	for (index = 0; index < size; index++)
		sprintf(&hex[2 * index], "%02X", buf[index]);
	printf("%s", hex);
}

void output_hex(const unsigned char* buf, const int size) {
	char hex[size * 2 + 1];
	unsigned int index;
	for (index = 0; index < size; index++)
		sprintf(&hex[2 * index], "%02X", buf[index]);
	printf("%s", hex);
}

void output_hex(std::vector<uint8_t>* vec) {
	char hex[3];
	unsigned int index;
	for (index = 0; index < vec->size(); index++) {
		sprintf(&hex[0], "%02X", vec->at(index));
		printf("%s", hex);
	}
}

/* An alternative is to use:
 * sqlite3 /data/system/locksettings.db "SELECT value FROM locksettings WHERE name='sp-handle' AND user=0;"
 * but we really don't want to include the 1.1MB libsqlite in TWRP. */
bool Find_Handle(const std::string& spblob_path, std::string& handle_str, long* handle) {
	DIR* dir = opendir(spblob_path.c_str());
	if (!dir) {
		printf("Error opening '%s'\n", spblob_path.c_str());
		return false;
	}

	struct dirent* de = 0;

	while ((de = readdir(dir)) != 0) {
		if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
			continue;
		size_t len = strlen(de->d_name);
		if (len <= 4)
			continue;
		char* p = de->d_name;
		p += len - 4;
		if (strncmp(p, ".pwd", 4) == 0) {
			handle_str = de->d_name;
			handle_str = handle_str.substr(0, len - 4);
			/* For some reason strtol does not work correctly here so we
			 * have to go through this ugle code to get the correct result.
			 * If you care to try to do better, use:
			 * 'b6f71045af7bd042' == -5262719748076220350
			 * Note that this code works on little endian CPUs and
			 * probably won't work on big endian without some changes. */
			//*handle = strtol(handle_str.c_str(), 0 , 16);
			if (handle_str.size() / 2 != sizeof(long)) {
				printf("handle_str '%s' with length of %lu does not match size of long for hex conversion\n", handle_str.c_str(), handle_str.size(), sizeof(long));
				return false;
			}
			unsigned char *p = (unsigned char*)handle;
			p += 7;
			unsigned int u;
			char s[handle_str.size()+1];
			strcpy(s, handle_str.c_str());
			const char* src = s;
			for (unsigned int i = 0; i < handle_str.size() / 2; i++) {
				sscanf(src, "%2x", &u);
				unsigned char bytes;
				bytes = u & 0xFF;
				*p = bytes;
				p--;
				src += 2;
			}
			closedir(dir);
			return true;
		}
	}
	closedir(dir);
	return false;
}

#include <algorithm>
template <class T>
void endianswap(T *objp) {
	unsigned char *memp = reinterpret_cast<unsigned char*>(objp);
	std::reverse(memp, memp + sizeof(T));
}

struct password_data_struct {
	int password_type;
	unsigned char scryptN;
	unsigned char scryptR;
	unsigned char scryptP;
	int salt_len;
	void* salt;
	int handle_len;
	void* password_handle;
};

bool Get_Password_Data(const std::string& spblob_path, const std::string& handle_str, password_data_struct *pwd) {
	std::string pwd_file = spblob_path + handle_str + ".pwd";
	std::string pwd_data;
	if (!android::base::ReadFileToString(pwd_file, &pwd_data)) {
		printf("Failed to read '%s'\n", pwd_file.c_str());
		return false;
	}
	output_hex(pwd_data.data(), pwd_data.size());printf("\n");
	const int* intptr = (const int*)pwd_data.data();
	pwd->password_type = *intptr;
	endianswap(&pwd->password_type);
	printf("password type %i\n", pwd->password_type);
	const unsigned char* byteptr = (const unsigned char*)pwd_data.data() + sizeof(int);
	pwd->scryptN = *byteptr;
	byteptr++;
	pwd->scryptR = *byteptr;
	byteptr++;
	pwd->scryptP = *byteptr;
	byteptr++;
	intptr = (const int*)byteptr;
	pwd->salt_len = *intptr;
	endianswap(&pwd->salt_len);
	if (pwd->salt_len != 0) {
		pwd->salt = new char[pwd->salt_len];
		if (!pwd->salt) {
			printf("Get_Password_Data malloc salt\n");
			return false;
		}
		memcpy(pwd->salt, intptr + 1, pwd->salt_len);
	} else {
		printf("Get_Password_Data salt_len is 0\n");
		return false;
	}
	return true;
}

extern "C" {
#include "crypto_scrypt.h"
}

bool Get_Password_Token(const password_data_struct *pwd, const std::string& Password, unsigned char* password_token) {
	if (!password_token) {
		printf("password_token is null\n");
		return false;
	}
	unsigned int N = 1 << pwd->scryptN;
	unsigned int r = 1 << pwd->scryptR;
	unsigned int p = 1 << pwd->scryptP;
	printf("N %i r %i p %i\n", N, r, p);
	//crypto_scrypt(unsigned char const*, unsigned long, unsigned char const*, unsigned long, unsigned long, unsigned int, unsigned int, unsigned char*, unsigned long)
	int ret = crypto_scrypt(reinterpret_cast<const uint8_t*>(Password.data()), Password.size(),
                          reinterpret_cast<const uint8_t*>(pwd->salt), pwd->salt_len,
                          N, r, p,
                          password_token, 32);
	//int ret = crypto_scrypt((const uint8_t *)Password.data(), Password.size(), (const uint8_t *)pwd->salt, (unsigned long)pwd->salt_len, N, r, p, password_token, 32);
	if (ret != 0) {
		printf("scrypt error\n");
		return false;
	}
	return true;
}

struct weaver_data_struct {
	unsigned char version;
	int slot;
};

bool Get_Weaver_Data(const std::string& spblob_path, const std::string& handle_str, weaver_data_struct *wd) {
	std::string weaver_file = spblob_path + handle_str + ".weaver";
	std::string weaver_data;
	if (!android::base::ReadFileToString(weaver_file, &weaver_data)) {
		printf("Failed to read '%s'\n", weaver_file.c_str());
		return false;
	}
	output_hex(weaver_data.data(), weaver_data.size());printf("\n");
	const unsigned char* byteptr = (const unsigned char*)weaver_data.data();
	wd->version = *byteptr;
	printf("weaver version %i\n", wd->version);
	const int* intptr = (const int*)weaver_data.data() + sizeof(unsigned char);
	wd->slot = *intptr;
	//endianswap(&wd->slot);
	printf("weaver slot %i\n", wd->slot);
	return true;
}

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

#include <keystore/IKeystoreService.h>
#include <binder/IPCThreadState.h>
#include <binder/IServiceManager.h>

#include <keystore/keystore.h>
#include <keystore/authorization_set.h>

#define SYNTHETIC_PASSWORD_VERSION 1
#define SYNTHETIC_PASSWORD_PASSWORD_BASED 0
#define SYNTHETIC_PASSWORD_KEY_PREFIX "USRSKEY_synthetic_password_"

namespace android { namespace keystore {

bool unwrapSyntheticPasswordBlob(const std::string& spblob_path, const std::string& handle_str, const userid_t user_id, const void* application_id, const size_t application_id_size) {
	std::string spblob_file = spblob_path + handle_str + ".spblob";
	std::string spblob_data;
	if (!android::base::ReadFileToString(spblob_file, &spblob_data)) {
		printf("Failed to read '%s'\n", spblob_file.c_str());
		return false;
	}
	const unsigned char* byteptr = (const unsigned char*)spblob_data.data();
	if (*byteptr != SYNTHETIC_PASSWORD_VERSION) {
		printf("SYNTHETIC_PASSWORD_VERSION does not match\n");
		return false;
	}
	byteptr++;
	if (*byteptr != SYNTHETIC_PASSWORD_PASSWORD_BASED) {
		printf("spblob data is not SYNTHETIC_PASSWORD_PASSWORD_BASED\n");
		return false;
	}
	byteptr++; // Now we're pointing to the blob itself fwiw
	void* personalized_application_id = PersonalizedHashBinary(PERSONALISATION_APPLICATION_ID, (const char*)application_id, application_id_size);
	printf("personalized application id: "); output_hex((unsigned char*)personalized_application_id, SHA512_DIGEST_LENGTH); printf("\n");
	OpenSSL_add_all_ciphers();
	int actual_size=0, final_size=0;
    EVP_CIPHER_CTX *d_ctx = EVP_CIPHER_CTX_new();
    const unsigned char* iv = (const unsigned char*)byteptr;
    printf("iv: "); output_hex((const unsigned char*)iv, 12); printf("\n");
    const unsigned char* cipher_text = (const unsigned char*)byteptr + 12;
    printf("cipher_text: "); output_hex((const unsigned char*)cipher_text, spblob_data.size() - 2 - 12); printf("\n");
    const unsigned char* key = (const unsigned char*)personalized_application_id;
    printf("key: "); output_hex((const unsigned char*)key, 32); printf("\n");
    EVP_DecryptInit(d_ctx, EVP_aes_256_gcm(), key, iv);
    std::vector<unsigned char> intermediate_key;
    intermediate_key.resize(spblob_data.size() - 2 - 12, '\0');
    EVP_DecryptUpdate(d_ctx, &intermediate_key[0], &actual_size, cipher_text, spblob_data.size() - 2 - 12);
    unsigned char tag[AES_BLOCK_SIZE];
    EVP_CIPHER_CTX_ctrl(d_ctx, EVP_CTRL_GCM_SET_TAG, 16, tag);
    EVP_DecryptFinal_ex(d_ctx, &intermediate_key[actual_size], &final_size);
    EVP_CIPHER_CTX_free(d_ctx);
    printf("spblob_data size: %i actual_size %i, final_size: %i\n", spblob_data.size(), actual_size, final_size);
    intermediate_key.resize(actual_size + final_size - 16, '\0');// not sure why we have to trim the size by 16 as I don't see this in java side
    printf("intermediate key: "); output_hex((const unsigned char*)intermediate_key.data(), intermediate_key.size()); printf("\n");

	/* AFAICT we only need a keyAlias which is USRSKEY_synthetic_password_b6f71045af7bd042 which we already have and a uid which is -1
	 * as the key data will be read again by the begin function later via the keystore.
	 * The data is in a hidl_vec format which consists of a type and a value. */
	::keystore::hidl_vec<uint8_t> data;
	std::string keystoreid = SYNTHETIC_PASSWORD_KEY_PREFIX;
	keystoreid += handle_str;
	sp<IServiceManager> sm = defaultServiceManager();
    sp<IBinder> binder = sm->getService(String16("android.security.keystore"));
    sp<IKeystoreService> service = interface_cast<IKeystoreService>(binder);

    if (service == NULL) {
        fprintf(stderr, "error: could not connect to keystore service\n");
        return 1;
    }
	int32_t ret = service->get(String16(keystoreid.c_str()), user_id, &data);
	if (ret < 0) {
		printf("Could not connect to keystore service %i\n", ret);
		return false;
	} else if (ret != 1 /*android::keystore::ResponseCode::NO_ERROR*/) {
		printf("keystore error: (%d)\n", /*responses[ret],*/ ret);
		return false;
	} else {
		printf("keystore returned: "); output_hex(&data[0], data.size()); printf("\n");
	}
	//::keystore::AuthorizationSet params;
	::keystore::hidl_vec<::keystore::KeyParameter> params;
	params.resize(5);
	params[0] = makeKeyParameter(::keystore::TAG_ALGORITHM, ::keystore::Algorithm::AES);
	params[1] = makeKeyParameter(::keystore::TAG_BLOCK_MODE, ::keystore::BlockMode::GCM);
	params[2] = makeKeyParameter(::keystore::TAG_PADDING, ::keystore::PaddingMode::NONE);
	::keystore::KeyParameter kpiv;
	kpiv.tag = ::keystore::TAG_PADDING;
	intermediate_key.resize(12);
	kpiv.blob = intermediate_key;
	params[3] = kpiv;
	params[4] = makeKeyParameter(::keystore::TAG_MAC_LENGTH, 128);
	//keymasterArgs.addEnum(KeymasterDefs.KM_TAG_ALGORITHM, KeymasterDefs.KM_ALGORITHM_AES);
	//keymasterArgs.addEnum(KeymasterDefs.KM_TAG_BLOCK_MODE, mKeymasterBlockMode);
	//keymasterArgs.addEnum(KeymasterDefs.KM_TAG_PADDING, mKeymasterPadding);
	//keymasterArgs.addUnsignedInt(KeymasterDefs.KM_TAG_MAC_LENGTH, mTagLengthBits);
	String16 keystoreid16(keystoreid.c_str());
	::keystore::KeyPurpose purpose = ::keystore::KeyPurpose::DECRYPT;
	OperationResult result;
	service->begin(binder, keystoreid16, purpose, true, params, data, -1, &result);
	ret = result.resultCode;
	if (ret < 0) {
		printf("Could not connect to keystore service %i\n", ret);
		return false;
	} else if (ret != 1 /*android::keystore::ResponseCode::NO_ERROR*/) {
		printf("keystore error: (%d)\n", /*responses[ret],*/ ret);
		return false;
	} else {
		printf("keystore begin operation successful\n");
	}
	::keystore::hidl_vec<uint8_t> signature;
	service->finish(binder, params, signature, data, &result);
	ret = result.resultCode;
	if (ret < 0) {
		printf("Could not connect to keystore service %i\n", ret);
		return false;
	} else if (ret != 1 /*android::keystore::ResponseCode::NO_ERROR*/) {
		printf("keystore error: (%d)\n", /*responses[ret],*/ ret);
		return false;
	} else {
		printf("keystore finish operation successful\n");
		printf("keystore finish returned: "); output_hex(&result.data[0], result.data.size()); printf("\n");
	}
	return false;
}

}}
	

#define PASSWORD_TOKEN_SIZE 32

bool Decrypt_User_Synth_Pass(const userid_t user_id, const std::string& Password2) {
	std::string Password = "0000";
	std::string secret = HashPassword(Password); // we're a long ways away from having this computed properly
	std::string token = "!";
	int flags = FLAG_STORAGE_DE;
    if (user_id == 0)
		flags = FLAG_STORAGE_DE;
	else
		flags = FLAG_STORAGE_CE;
	char spblob_path_char[PATH_MAX];
	sprintf(spblob_path_char, "/data/system_de/%d/spblob/", user_id);
	std::string spblob_path = spblob_path_char;
	long handle = 0;
	std::string handle_str;
	if (!Find_Handle(spblob_path, handle_str, &handle)) {
		printf("Error getting handle\n");
		return false;
	}
	printf("Handle is '%s' or %ld\n", handle_str.c_str(), handle);
	password_data_struct pwd;
	if (!Get_Password_Data(spblob_path, handle_str, &pwd)) {
		printf("Failed to Get_Password_Data\n");
		return false;
	}
	printf("pwd N %i R %i P %i salt ", pwd.scryptN, pwd.scryptR, pwd.scryptP); output_hex((char*)pwd.salt, pwd.salt_len); printf("\n");
	unsigned char password_token[PASSWORD_TOKEN_SIZE];
	printf("Password: '%s' %lu\n", Password.c_str(), sizeof(long long));
	if (!Get_Password_Token(&pwd, Password, &password_token[0]))
		return false;
	output_hex(&password_token[0], PASSWORD_TOKEN_SIZE);printf("\n");
	// BEGIN PIXEL 2 WEAVER
	weaver_data_struct wd;
	if (!Get_Weaver_Data(spblob_path, handle_str, &wd)) {
		printf("Failed to get weaver data\n");
		// Fail over to gatekeeper path for Pixel 1???
		return false;
	}
	void* weaver_key = PersonalizedHashBinary(PERSONALISATION_WEAVER_KEY, (char*)&password_token[0], PASSWORD_TOKEN_SIZE);
	if (!weaver_key) {
		printf("malloc error getting wvr_key\n");
		return false;
	}
	android::vold::Weaver weaver;
    if (!weaver) {
		printf("Failed to get weaver service\n");
		return false;
	} else {
		printf("Got weaver service!\n");
	}
	uint32_t weaver_key_size = 0;
	if (!weaver.GetKeySize(&weaver_key_size)) {
		printf("Failed to get weaver key size\n");
		return false;
	} else {
		printf("weaver key size is %u\n", weaver_key_size);
	}
	printf("weaver key: "); output_hex((unsigned char*)weaver_key, weaver_key_size); printf("\n");
	std::vector<uint8_t> weaver_payload;
	if (!weaver.WeaverVerify(wd.slot, weaver_key, &weaver_payload)) {
		printf("failed to weaver verify\n");
		return false;
	}
	printf("weaver payload: "); output_hex(&weaver_payload); printf("\n");
	void* weaver_secret = PersonalizedHashBinary(PERSONALISATION_WEAVER_PASSWORD, (const char*)weaver_payload.data(), weaver_payload.size());
	printf("weaver secret: "); output_hex((unsigned char*)weaver_secret, SHA512_DIGEST_LENGTH); printf("\n");
	char application_id[PASSWORD_TOKEN_SIZE + SHA512_DIGEST_LENGTH];
	memcpy((void*)&application_id[0], (void*)&password_token[0], PASSWORD_TOKEN_SIZE);
	memcpy((void*)&application_id[PASSWORD_TOKEN_SIZE], weaver_secret, SHA512_DIGEST_LENGTH);
	printf("application ID: "); output_hex((unsigned char*)application_id, PASSWORD_TOKEN_SIZE + SHA512_DIGEST_LENGTH); printf("\n");
	// END PIXEL 2 WEAVER
	if (!android::keystore::unwrapSyntheticPasswordBlob(spblob_path, handle_str, user_id, (const void*)&application_id[0], PASSWORD_TOKEN_SIZE + SHA512_DIGEST_LENGTH)) {
		printf("failed to unwrapSyntheticPasswordBlob\n");
		return false;
	}
	delete(pwd.salt); // can't do this on a void so need to switch to malloc and free I think
	// need to free up various other memory blocks too
	return false;
	if (!e4crypt_unlock_user_key(user_id, 0, token.c_str(), secret.c_str())) {
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

bool Decrypt_User(const userid_t user_id, const std::string& Password) {
    uint8_t *auth_token;
    uint32_t auth_token_len;
    int ret;

    struct stat st;
    if (user_id > 9999) {
		printf("user_id is too big\n");
		return false;
	}
	if (stat("/data/system_de/0/spblob", &st) == 0) {
		printf("Using synthetic password method\n");
		return Decrypt_User_Synth_Pass(user_id, Password);
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
