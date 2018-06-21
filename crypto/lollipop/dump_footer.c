/*
 * Copyright (C) 2010 The Android Open Source Project
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

#include <errno.h>
#include <sys/ioctl.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/fs.h>

#define CRYPT_FOOTER_OFFSET 0x4000

char* default_path = "/dev/block/bootdevice/by-name/userdata";

static unsigned int get_blkdev_size(int fd) {
	unsigned long nr_sec;

	if ( (ioctl(fd, BLKGETSIZE, &nr_sec)) == -1) {
		nr_sec = 0;
	}

	return (unsigned int) nr_sec;
}

int main(int argc, char *argv[]) {
	char* userdata_path = argv[1];
	if (argc < 2)
		userdata_path = default_path;
	int rfd, wfd;
	rfd = open(userdata_path, O_RDONLY);
	if (rfd < 0) {
		printf("Cannot open '%s': %s\n", userdata_path, strerror(errno));
		return -1;
	}
	unsigned int nr_sec;
	off64_t offset;
	if ((nr_sec = get_blkdev_size(rfd))) {
		offset = ((off64_t)nr_sec * 512) - CRYPT_FOOTER_OFFSET;
	} else {
		printf("Failed to get offset\n");
		close(rfd);
		return -1;
	}
	if (lseek64(rfd, offset, SEEK_SET) == -1) {
		printf("Failed to lseek64\n");
		close(rfd);
		return -1;
	}
	char buffer[16 * 1024];
	if (read(rfd, buffer, sizeof(buffer)) != sizeof(buffer)) {
		printf("Failed to read: %s\n", strerror(errno));
		close(rfd);
		return -1;
	}
	close(rfd);
	wfd = open("/footer", O_WRONLY | O_CREAT, 0644);
	if (wfd < 0) {
		printf("Cannot open '/footer': %s\n", strerror(errno));
		return -1;
	}
	if (write(wfd, buffer, sizeof(buffer)) != sizeof(buffer)) {
		printf("Failed to write: %s\n", strerror(errno));
		close(wfd);
		return -1;
	}
	close(wfd);
	return 0;
}
