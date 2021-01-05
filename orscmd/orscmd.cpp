/*
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

#define __STDC_FORMAT_MACROS 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <inttypes.h>

// for setcap and getcap
#include <sys/capability.h>
#include <sys/xattr.h>
#include <linux/xattr.h>

#include "orscmd.h"
#include "../variables.h"

void print_version(void) {
	printf("TWRP openrecoveryscript command line tool, TWRP version %s\n\n", TW_VERSION_STR);
}

void print_usage(void) {
	print_version();
	printf("Allows command line usage of TWRP via openrecoveryscript commands.\n");
	printf("Some common commands include:\n");
	printf("  install /path/to/update.zip\n");
	printf("  backup <SDCRBAEM> [backupname]\n");
	printf("  restore <SDCRBAEM> [backupname]\n");
	printf("  wipe <partition name>\n");
	printf("  format data\n");
	printf("  sideload\n");
	printf("  set <variable> [value]\n");
	printf("  decrypt <password> [USER ID]\n");
	printf("  remountrw\n");
	printf("  fixperms\n");
	printf("  mount <path>\n");
	printf("  unmount <path>\n");
	printf("  listmounts\n");
	printf("  print <value>\n");
	printf("  mkdir <directory>\n");
	printf("  reboot [recovery|poweroff|bootloader|download|edl]\n");
	printf("\nSee more documentation at https://twrp.me/faq/openrecoveryscript.html\n");
}

int do_setcap(const char* filename, const char* capabilities)
{
	uint64_t caps;
	if (sscanf(capabilities, "%" SCNi64, &caps) != 1)
	{
		printf("setcap: invalid capabilities \"%s\"\n", filename);
		return 1;
	}
	struct vfs_cap_data cap_data;
	memset(&cap_data, 0, sizeof(cap_data));
	cap_data.magic_etc = VFS_CAP_REVISION | VFS_CAP_FLAGS_EFFECTIVE;
	cap_data.data[0].permitted = (uint32_t) (caps & 0xffffffff);
	cap_data.data[0].inheritable = 0;
	cap_data.data[1].permitted = (uint32_t) (caps >> 32);
	cap_data.data[1].inheritable = 0;
	if (setxattr(filename, XATTR_NAME_CAPS, &cap_data, sizeof(cap_data), 0) < 0) {
		printf("setcap of %s to %" PRIx64 " failed: %s\n",
				filename, caps, strerror(errno));
		return 1;
	}
	return 0;
}

int do_getcap(const char* filename)
{
	struct vfs_cap_data cap_data;
	memset(&cap_data, 0, sizeof(cap_data));
	int rc = getxattr(filename, XATTR_NAME_CAPS, &cap_data, sizeof(vfs_cap_data));
	if (rc > 0)
	{
		uint64_t caps = (uint64_t) cap_data.data[1].permitted << 32 | cap_data.data[0].permitted;
		printf("0x%" PRIx64 "\n", caps);
	}
	else
		printf("getcap of %s failed: %s\n", filename, strerror(errno));

	return rc > 0;
}

int main(int argc, char **argv) {
	int read_fd, write_fd, index;
	char command[1024], result[512];

	if (argc < 2 || strcmp(argv[1], "help") == 0 || strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "?") == 0 || strcmp(argv[1], "-h") == 0) {
		print_usage();
		return 0;
	}
	if (strcmp(argv[1], "version") == 0 || strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--version") == 0) {
		print_version();
		return 0;
	}

	if (strcmp(argv[1], "setcap") == 0) {
		if (argc != 4)
		{
			printf("Usage: setcap filename capabilities\n\n"
				"capabilities must be specified as a number. Prefix with 0x for hexadecimal.\n");
			return 1;
		}
		return do_setcap(argv[2], argv[3]);
	}

	if (strcmp(argv[1], "getcap") == 0) {
		if (argc != 3)
		{
			printf("Usage: getcap filename\n");
			return 1;
		}
		return do_getcap(argv[2]);
	}

	sprintf(command, "%s", argv[1]);
	for (index = 2; index < argc; index++) {
		sprintf(command, "%s %s", command, argv[index]);
	}

	write_fd = open(ORS_INPUT_FILE, O_WRONLY);
	if (write_fd < 0) {
		printf("TWRP does not appear to be running. Waiting for TWRP to start . . .\n");
		printf("Press CTRL + C to quit.\n");
		while (write_fd < 0)
			write_fd = open(ORS_INPUT_FILE, O_WRONLY);
	}
	if (write(write_fd, command, sizeof(command)) != sizeof(command)) {
		printf("Error sending command.\n");
		close(write_fd);
		return -1;
	}
	read_fd = open(ORS_OUTPUT_FILE, O_RDONLY);
	if (read_fd < 0) {
		printf("Unable to open %s for read.\n", ORS_OUTPUT_FILE);
		return -1;
	}
	memset(&result, 0, sizeof(result));
	while (read(read_fd, &result, sizeof(result)) > 0) {
		result[510] = '\n';
		result[511] = '\0';
		printf("%s", result);
		memset(&result, 0, sizeof(result));
	}
	close(write_fd);
	close(read_fd);
	return 0;
}
