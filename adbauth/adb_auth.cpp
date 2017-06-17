
#include "adb_auth.hpp"

#include <iostream>
#include <fstream>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "../data.hpp"

#define ADBD_SOCKET_PATH "/dev/socket/adbd"
#define BUFFER_SIZE 4096

//#define ADB_AUTH_DEBUG

/* Allow ADB from unknown/unauthenticated hosts
 *
 * As part of authenticating a host to a client (e.g. a phone), the phone
 * receives a public key from the host.
 * First it checks /data/misc/adb/adb_keys for the correct key. If it is
 * listed in the file, the host is authenticated. Otherwise, it sends a
 * request with the public key to the UsbDebuggingManager [1]. This
 * manager shows a popup with the serial key. It will then send back to
 * adbd whether the user accepted or rejected the key. Optionally, if the
 * "Always allow from this computer" checkbox is checked, it will also
 * append the key to /data/misc/adb/adb_keys.
 *
 * The protocol between adbd and the UI is very simple, and is defined
 * in [2]. In short, it sends the string "PK" + the base64-encoded public
 * key to the UI, and expects the string "OK" or "NO" back. It looks like
 * that's all there is to it.
 *
 * [1]: frameworks/base/services/usb/java/com/android/server/usb/UsbDebuggingManager.java
 * [2]: system/core/adb/adb_auth_client.cpp
 */

std::ofstream log;

// Start the thread
void adb_auth_start() {
#ifdef ADB_AUTH_DEBUG
	log.open("/tmp/adbauth.log");
#endif
	adb_auth_log("starting adbauth thread!\n");

	pthread_t thread;
	pthread_create(&thread, NULL, &adb_auth_main, NULL);
}

void adb_auth_log(std::string msg) {
#ifdef ADB_AUTH_DEBUG
	log << msg;
	log.flush();
#endif
}

// Main thread
void* adb_auth_main(void *arg __unused) {
	adb_auth_log("started thread\n");

	while (true) {
		adb_auth_handle_adbd();
		// The adbd process stopped, for example because sideload was
		// started.
		// Don't keep spinning with new connection requests.
		sleep(1);
	}

	return NULL;
}

void adb_auth_handle_adbd() {
	int sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sock < 0) {
		adb_auth_log("couldn't create socket fd?\n");
		return;
	}

	struct sockaddr_un remote;
	remote.sun_family = AF_UNIX;
	strncpy(remote.sun_path, ADBD_SOCKET_PATH, sizeof(remote.sun_path));
	if (connect(sock, (struct sockaddr *)&remote, sizeof(remote)) < 0) {
		adb_auth_log("could not connect to socket " ADBD_SOCKET_PATH "\n");
		return;
	}

	adb_auth_log("connected to adbd socket\n");

	while (true) {
		char buffer[BUFFER_SIZE];
		int count = recv(sock, buffer, BUFFER_SIZE, 0);
		if (count <= 0) break; // EOF
		if (count < 2) {
			adb_auth_log("buffer too small\n");
			continue; // not sure what to do with this - does it happen?
		}
		if (buffer[0] == 'P' && buffer[1] == 'K') {
			adb_auth_log("got public key auth request\n");
			int accept = DataManager::GetIntValue("tw_adb_accept_any_host");
			if (accept) {
				adb_auth_log("accepting connection\n");
				send(sock, "OK", 2, 0);
			} else {
				adb_auth_log("rejecting connection\n");
				send(sock, "NO", 2, 0);
			}
		} else {
			adb_auth_log("got unknown request\n");
		}
	}

	adb_auth_log("exiting\n");
	close(sock);
}
