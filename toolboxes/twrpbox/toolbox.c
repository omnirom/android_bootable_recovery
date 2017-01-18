#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int, char **);
static int toolbox_main(int , char **);

#define TOOL(name) int name##_main(int, char**);
#include "tools.h"
#undef TOOL

static struct
{
    const char *name;
    int (*func)(int, char**);
} tools[] = {
    { "twrpbox", toolbox_main },
#define TOOL(name) { #name, name##_main },
#include "tools.h"
#undef TOOL
    { 0, 0 },
};

static int toolbox_main(int argc, char **argv)
{
    int i;

    // "toolbox foo ..." is equivalent to "foo ..."
    if (argc > 1) {
        return main(argc - 1, argv + 1);
    } else {
        printf("TWRP Toolbox!\n");
        printf("Provided tools:");
        for (i = 1; tools[i].name; i++)
            printf(" %s", tools[i].name);
        printf("\n");
        return 0;
    }
}

static void SIGPIPE_handler(int signal __unused) {
    // Those desktop Linux tools that catch SIGPIPE seem to agree that it's
    // a successful way to exit, not a failure. (Which makes sense --- we were
    // told to stop by a reader, rather than failing to continue ourselves.)
    _exit(0);
}

int main(int argc, char **argv)
{
    int i;
    char *name = argv[0];

    // Let's assume that none of this code handles broken pipes. At least ls,
    // ps, and top were broken (though I'd previously added this fix locally
    // to top). We exit rather than use SIG_IGN because tools like top will
    // just keep on writing to nowhere forever if we don't stop them.
    signal(SIGPIPE, SIGPIPE_handler);

    if((argc > 1) && (argv[1][0] == '@')) {
        name = argv[1] + 1;
        argc--;
        argv++;
    } else {
        char *cmd = strrchr(argv[0], '/');
        if (cmd)
            name = cmd + 1;
    }

    for(i = 0; tools[i].name; i++){
        if(!strcmp(tools[i].name, name)){
            return tools[i].func(argc, argv);
        }
    }

    printf("%s: no such tool\n", argv[0]);
    return -1;
}
