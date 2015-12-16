#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <cutils/android_reboot.h>
#include <unistd.h>

int reboot_main(int argc, char *argv[])
{
    int ret;
    int nosync = 0;
    int poweroff = 0;
    int flags = 0;

    opterr = 0;
    do {
        int c;

        c = getopt(argc, argv, "np");
        
        if (c == EOF) {
            break;
        }
        
        switch (c) {
        case 'n':
            nosync = 1;
            break;
        case 'p':
            poweroff = 1;
            break;
        case '?':
            fprintf(stderr, "usage: %s [-n] [-p] [rebootcommand]\n", argv[0]);
            exit(EXIT_FAILURE);
        }
    } while (1);

    if(argc > optind + 1) {
        fprintf(stderr, "%s: too many arguments\n", argv[0]);
        exit(EXIT_FAILURE);
    }

#if 0 // This isn't supported anymore. Sync, always
    if(nosync)
        /* also set NO_REMOUNT_RO as remount ro includes an implicit sync */
        flags = ANDROID_RB_FLAG_NO_SYNC | ANDROID_RB_FLAG_NO_REMOUNT_RO;
#endif

    if(poweroff)
        ret = android_reboot(ANDROID_RB_POWEROFF, flags, 0);
    else if(argc > optind)
        ret = android_reboot(ANDROID_RB_RESTART2, flags, argv[optind]);
    else
        ret = android_reboot(ANDROID_RB_RESTART, flags, 0);
    if(ret < 0) {
        perror("reboot");
        exit(EXIT_FAILURE);
    }
    fprintf(stderr, "reboot returned\n");
    return 0;
}
