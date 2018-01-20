/*
 * Copyright (C) 2016 Team Win Recovery Project
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

/*
 * This computes the "secret" used by Android as one of the parameters
 * to decrypt File Based Encryption. The secret is prefixed with
 * "Android FBE credential hash" padded with 0s to 128 bytes then the
 * user's password is appended to the end of the 128 bytes. This string
 * is then hashed with sha512 and the sha512 value is then converted to
 * hex with upper-case characters.
 */

#include <stdio.h>
#include <string>
#include <stdlib.h>
#include <openssl/sha.h>

#include "HashPassword.h"

#define PASS_PADDING_SIZE 128
#define SHA512_HEX_SIZE SHA512_DIGEST_LENGTH * 2

void* PersonalizedHashBinary(const char* prefix, const char* key, const size_t key_size) {
	size_t size = PASS_PADDING_SIZE + key_size;
	unsigned char* buffer = (unsigned char*)calloc(1, size);
	if (!buffer) return NULL; // failed to malloc
	memcpy((void*)buffer, (void*)prefix, strlen(prefix));
	unsigned char* ptr = buffer + PASS_PADDING_SIZE;
	memcpy((void*)ptr, key, key_size);
	unsigned char hash[SHA512_DIGEST_LENGTH];
	SHA512_CTX sha512;
	SHA512_Init(&sha512);
	SHA512_Update(&sha512, buffer, size);
	SHA512_Final(hash, &sha512);
	free(buffer);
	void* ret = malloc(SHA512_DIGEST_LENGTH);
	if (!ret) return NULL; // failed to malloc
	memcpy(ret, (void*)&hash[0], SHA512_DIGEST_LENGTH);
	return ret;
}

std::string PersonalizedHash(const char* prefix, const char* key, const size_t key_size) {
	size_t size = PASS_PADDING_SIZE + key_size;
	unsigned char* buffer = (unsigned char*)calloc(1, size);
	if (!buffer) return ""; // failed to malloc
	memcpy((void*)buffer, (void*)prefix, strlen(prefix));
	unsigned char* ptr = buffer + PASS_PADDING_SIZE;
	memcpy((void*)ptr, key, key_size);
	unsigned char hash[SHA512_DIGEST_LENGTH];
	SHA512_CTX sha512;
	SHA512_Init(&sha512);
	SHA512_Update(&sha512, buffer, size);
	SHA512_Final(hash, &sha512);
	int index = 0;
	char hex_hash[SHA512_HEX_SIZE + 1];
	for(index = 0; index < SHA512_DIGEST_LENGTH; index++)
		sprintf(hex_hash + (index * 2), "%02X", hash[index]);
	hex_hash[128] = 0;
	std::string ret = hex_hash;
	free(buffer);
	return ret;
}

std::string PersonalizedHash(const char* prefix, const std::string& Password) {
	return PersonalizedHash(prefix, Password.c_str(), Password.size());
}

std::string HashPassword(const std::string& Password) {
	const char* prefix = FBE_PERSONALIZATION;
	return PersonalizedHash(prefix, Password);
}
