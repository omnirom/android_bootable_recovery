#include <stdio.h>
#include <unistd.h>
#include <string>
#include <dirent.h>
#include <stdlib.h>
#include <linux/capability.h>
#include <linux/xattr.h>
#include <sys/xattr.h>
#include <sys/vfs.h>
#include <errno.h>

#include "libcp_xattrs.h"

static bool cp_xattrs_list_xattrs_callback(const std::string& from,
        bool (*callback)(const char *attr, const char *data, ssize_t data_size, void *cookie),
        void *cookie)
{
    ssize_t res;
    char static_names[512];
    char static_data[256];
    char *names = static_names;
    char *data = static_data;
    char *names_itr;
    ssize_t list_size;
    ssize_t value_size;
    ssize_t data_size = sizeof(static_data);

    list_size = llistxattr(from.c_str(), NULL, 0);
    if(list_size == 0)
        return true;
    else if(list_size < 0)
    {
        fprintf(stderr, "Failed to llistxattr on %s: %s", from.c_str(), strerror(errno));
        return false;
    }
    else if(list_size > 16*1024)
    {
        fprintf(stderr, "Failed to llistxattr: list is too long! %s (%d)", from.c_str(), list_size);
        return false;
    }

    if(list_size > (ssize_t)sizeof(static_names))
    {
        names = (char*)malloc(list_size);
        printf("alloc names %d\n", list_size);
    }

    list_size = llistxattr(from.c_str(), names, list_size);
    if(list_size < 0)
    {
        fprintf(stderr, "Failed to llistxattr on %s: %s", from.c_str(), strerror(errno));
        return false;
    }

    for(names_itr = names; names_itr < names + list_size; names_itr += strlen(names_itr) + 1)
    {
        value_size = lgetxattr(from.c_str(), names_itr, NULL, 0);
        if(value_size < 0)
        {
            if(errno == ENOENT)
                continue;
            fprintf(stderr, "Failed lgetxattr on %s: %d (%s)\n", from.c_str(), errno, strerror(errno));
            return false;
        }

        if(value_size > 16*1024)
        {
            fprintf(stderr, "Failed to lgetxattr: value is too long! %s (%d)", from.c_str(), value_size);
            return false;
        }

        if(value_size > (ssize_t)sizeof(static_data) && value_size > data_size)
        {
            if(data == static_data)
                data = NULL;
            data_size = value_size;
            data = (char*)realloc(data, data_size);
            printf("alloc data %d\n", data_size);
        }

        res = lgetxattr(from.c_str(), names_itr, data, value_size);
        if(res < 0)
        {
            fprintf(stderr, "Failed lgetxattr on %s: %d (%s)\n", from.c_str(), errno, strerror(errno));
            return false;
        }

        if(!callback(names_itr, data, value_size, cookie))
            return false;
    }

    if(names != static_names)
        free(names);
    if(data != static_data)
        free(data);

    return true;
}

static bool cp_xattrs_single_file_callback(const char *attr, const char *data, ssize_t data_size, void *cookie)
{
    std::string *to = (std::string*)cookie;
    ssize_t res = lsetxattr(to->c_str(), attr, data, data_size, 0);
    if(res < 0 && errno != ENOENT)
    {
        fprintf(stderr, "Failed to lsetxattr %s on %s: %d (%s)\n", attr, to->c_str(), errno, strerror(errno));
        return false;
    }
    return true;
}

bool cp_xattrs_single_file(const std::string& from, const std::string& to)
{
    return cp_xattrs_list_xattrs_callback(from, &cp_xattrs_single_file_callback, (void*)&to);
}

static bool cp_xattrs_list_xattrs_map_callback(const char *attr, const char *data, ssize_t data_size, void *cookie)
{
    std::map<std::string, std::vector<char> > *res = (std::map<std::string, std::vector<char> >*)cookie;
    std::vector<char> data_vec(data, data+data_size);
    res->insert(std::make_pair(attr, data_vec));
    return true;
}

bool cp_xattrs_list_xattrs(const std::string& path, std::map<std::string, std::vector<char> > &res)
{
    return cp_xattrs_list_xattrs_callback(path, &cp_xattrs_list_xattrs_map_callback, (void*)&res);
}

bool cp_xattrs_recursive(const std::string& from, const std::string& to, unsigned char type)
{
    if(!cp_xattrs_single_file(from, to))
        return false;

    if(type != DT_DIR)
        return true;

    DIR *d;
    struct dirent *dt;

    d = opendir(from.c_str());
    if(!d)
    {
        fprintf(stderr, "Failed to open dir %s\n", from.c_str());
        return false;
    }

    while((dt = readdir(d)))
    {
        if (dt->d_type == DT_DIR && dt->d_name[0] == '.' &&
            (dt->d_name[1] == '.' || dt->d_name[1] == 0))
            continue;

        if(!cp_xattrs_recursive(from + "/" + dt->d_name, to + "/" + dt->d_name, dt->d_type))
        {
            closedir(d);
            return false;
        }
    }

    closedir(d);
    return true;
}
