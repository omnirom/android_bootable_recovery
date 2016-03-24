#include <stdio.h>
#include <string>
#include <dirent.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "libcp_xattrs.h"

int main(int argc, char *argv[])
{
    std::string from, to;
    struct stat info;

    if(argc >= 3)
    {
        from = argv[1];
        to = argv[2];
    }
    else
    {
        printf("Usage: %s SOURCE DEST\n", argv[0]);
        return 1;
    }

    if(lstat(from.c_str(), &info) < 0)
    {
        fprintf(stderr, "lstat on %s failed: %s\n", from.c_str(), strerror(errno));
        return 1;
    }

    return cp_xattrs_recursive(from, to, S_ISDIR(info.st_mode) ? DT_DIR : DT_REG) ? 0 : 1;
}
