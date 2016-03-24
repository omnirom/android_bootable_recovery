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
    int i;
    std::map<std::string, std::vector<char> > attrs;
    std::map<std::string, std::vector<char> >::iterator itr;

    if(argc < 2)
    {
        printf("Usage: %s FILE(s)\n", argv[0]);
        return 1;
    }

    for(i = 1; i < argc; ++i)
    {
        printf("%s:\n", argv[i]);
        if(!cp_xattrs_list_xattrs(argv[i], attrs))
            return -1;

        for(itr = attrs.begin(); itr != attrs.end(); ++itr)
        {
            printf("  %s: ", itr->first.c_str());
            for(size_t c = 0; c < itr->second.size(); ++c)
                printf("%c", itr->second[c]);
            printf("\n");
        }
        printf("\n");
        attrs.clear();
    }
    return 0;
}
