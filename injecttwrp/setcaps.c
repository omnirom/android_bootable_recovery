#include <sys/capability.h>
#include <sys/xattr.h>
#include <linux/xattr.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
	struct vfs_cap_data cap_data;
	uint64_t capabilities = (1 << CAP_SETUID) | (1 << CAP_SETGID);

	if (argc != 2) {
		fprintf(stderr, "Usage: %s path\n", argv[0]);
		exit(EXIT_FAILURE);
	}
	memset(&cap_data, 0, sizeof(cap_data));
	cap_data.magic_etc = VFS_CAP_REVISION | VFS_CAP_FLAGS_EFFECTIVE;
	cap_data.data[0].permitted = (uint32_t) (capabilities & 0xffffffff);
	cap_data.data[0].inheritable = 0;
	cap_data.data[1].permitted = (uint32_t) (capabilities >> 32);
	cap_data.data[1].inheritable = 0;
	if (setxattr(argv[1], XATTR_NAME_CAPS, &cap_data, sizeof(cap_data), 0) < 0) {
		printf("Failed to reset capabilities\n");
		return -1;
	}
	return 0;
}
