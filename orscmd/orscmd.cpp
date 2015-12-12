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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

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
	printf("  backup BSDC backupname\n");
	printf("  restore backupname BSDC\n");
	printf("  factoryreset\n");
	printf("  wipe cache\n");
	printf("  sideload\n");
	printf("  set variable value\n");
	printf("  get variable\n");
	printf("  decrypt password\n");
	printf("  remountrw\n");
	printf("\nSee more documentation at http://teamw.in/openrecoveryscript\n");
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
