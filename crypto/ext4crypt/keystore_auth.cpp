/*
	Copyright 2018 bigbiff/Dees_Troy TeamWin
	This file is part of TWRP/TeamWin Recovery Project.

	TWRP is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	TWRP is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with TWRP.  If not, see <http://www.gnu.org/licenses/>.
*/

/* The keystore refuses to allow the root user to supply auth tokens, so
 * we write the auth token to a file in TWRP and run a separate service
 * (this) that runs as the system user to add the auth token. TWRP waits
 * for /auth_token to be deleted and also looks for /auth_error to check
 * for errors. TWRP will error out after a while if /auth_token does not
 * get deleted. */

#include <stdio.h>
#include <string>

#ifdef USE_SECURITY_NAMESPACE
#include <android/security/IKeystoreService.h>
#else
#include <keystore/IKeystoreService.h>
#include <keystore/authorization_set.h>
#endif
#include <binder/IPCThreadState.h>
#include <binder/IServiceManager.h>

#include <keystore/keystore.h>

#ifndef LOG_TAG
#define LOG_TAG "keystore_auth"
#endif

using namespace android;

void create_error_file() {
	FILE* error_file = fopen("/auth_error", "wb");
	if (error_file == NULL) {
		printf("Failed to open /auth_error\n");
		ALOGE("Failed to open /auth_error\n");
		return;
	}
	fwrite("1", 1, 1, error_file);
	fclose(error_file);
	unlink("/auth_token");
}

int main() {
	unlink("/auth_error");
	FILE* auth_file = fopen("/auth_token", "rb");
	if (auth_file == NULL) {
		printf("Failed to open /auth_token\n");
		ALOGE("Failed to open /auth_token\n");
		create_error_file();
		return -1;
	}
	// Get the file size
	fseek(auth_file, 0, SEEK_END);
	int size = ftell(auth_file);
	fseek(auth_file, 0, SEEK_SET);
	uint8_t auth_token[size];
	fread(auth_token , sizeof(uint8_t), size, auth_file);
	fclose(auth_file);
	// First get the keystore service
	sp<IServiceManager> sm = defaultServiceManager();
	sp<IBinder> binder = sm->getService(String16("android.security.keystore"));
#ifdef USE_SECURITY_NAMESPACE
	sp<security::IKeystoreService> service = interface_cast<security::IKeystoreService>(binder);
#else
	sp<IKeystoreService> service = interface_cast<IKeystoreService>(binder);
#endif
	if (service == NULL) {
		printf("error: could not connect to keystore service\n");
		ALOGE("error: could not connect to keystore service\n");
		create_error_file();
		return -2;
	}
#ifdef USE_SECURITY_NAMESPACE
	std::vector<uint8_t> auth_token_vector(&auth_token[0], (&auth_token[0]) + size);
	int result = 0;
	auto binder_result = service->addAuthToken(auth_token_vector, &result);
	if (!binder_result.isOk() || !keystore::KeyStoreServiceReturnCode(result).isOk()) {
#else
	::keystore::KeyStoreServiceReturnCode auth_result = service->addAuthToken(auth_token, size);
	if (!auth_result.isOk()) {
#endif
		// The keystore checks the uid of the calling process and will return a permission denied on this operation for user 0
		printf("keystore error adding auth token\n");
		ALOGE("keystore error adding auth token\n");
		create_error_file();
		return -3;
	}
	printf("successfully added auth token to keystore\n");
	ALOGD("successfully added auth token to keystore\n");
	unlink("/auth_token");
	return 0;
}
