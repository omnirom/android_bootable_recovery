#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

static int usage()
{
    fprintf(stderr,"ln [-s] <target> <name>\n");
    return -1;
}

int ln_main(int argc, char *argv[])
{
    int symbolic = 0;
    int ret;
    if(argc < 2) return usage();
    
    if(!strcmp(argv[1],"-s")) {
        symbolic = 1;
        argc--;
        argv++;
    }

    if(argc < 3) return usage();

    if(symbolic) {
        ret = symlink(argv[1], argv[2]);
    } else {
        ret = link(argv[1], argv[2]);
    }
    if(ret < 0)
        fprintf(stderr, "link failed %s\n", strerror(errno));
    return ret;
}
