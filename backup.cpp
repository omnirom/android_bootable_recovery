#include <stdlib.h>
#include <stdio.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/vfs.h>
#include <time.h>

#include "cutils/properties.h"

#include "roots.h"

#include "bu.h"

using namespace android;

static int append_sod(const char* opt_hash)
{
    const char* key;
    char value[PROPERTY_VALUE_MAX];
    int len;
    char buf[PROP_LINE_LEN];
    char sodbuf[PROP_LINE_LEN*10];
    char* p = sodbuf;

    key = "hash.name";
    strcpy(value, opt_hash);
    p += sprintf(p, "%s=%s\n", key, value);

    key = "ro.build.product";
    property_get(key, value, "");
    p += sprintf(p, "%s=%s\n", key, value);

    for (int i = 0; i < MAX_PART; ++i) {
        partspec* part = part_get(i);
        if (!part)
            break;
        const char* fstype = part->vol->fs_type;
        uint64_t size, used;
        if (!strcmp(fstype, "mtd") || !strcmp(fstype, "bml") || !strcmp(fstype, "emmc")) {
            int fd = open(part->vol->blk_device, O_RDONLY);
            part->size = part->used = lseek(fd, 0, SEEK_END);
            close(fd);
        }
        else {
            struct statfs stfs;
            memset(&stfs, 0, sizeof(stfs));
            if (ensure_path_mounted(part->path) != 0) {
                logmsg("append_sod: failed to mount %s\n", part->path);
                continue;
            }
            if (statfs(part->path, &stfs) == 0) {
                part->size = (stfs.f_blocks) * stfs.f_bsize;
                part->used = (stfs.f_blocks - stfs.f_bfree) * stfs.f_bsize;
            }
            else {
                logmsg("Failed to statfs %s: %s\n", part->path, strerror(errno));
            }
            ensure_path_unmounted(part->path);
        }
        p += sprintf(p, "fs.%s.size=%llu\n", part->name, part->size);
        p += sprintf(p, "fs.%s.used=%llu\n", part->name, part->used);
    }

    int rc = tar_append_file_contents(tar, "SOD", 0600,
            getuid(), getgid(), sodbuf, p-sodbuf);
    return rc;
}

static int append_eod(const char* opt_hash)
{
    char buf[PROP_LINE_LEN];
    char eodbuf[PROP_LINE_LEN*10];
    char* p = eodbuf;
    int n;

    p += sprintf(p, "hash.datalen=%u\n", hash_datalen);

    unsigned char digest[HASH_MAX_LENGTH];
    char hexdigest[HASH_MAX_STRING_LENGTH];

    if (!strcasecmp(opt_hash, "sha1")) {
        SHA1_Final(digest, &sha_ctx);
        for (n = 0; n < SHA_DIGEST_LENGTH; ++n) {
            sprintf(hexdigest+2*n, "%02x", digest[n]);
        }
        p += sprintf(p, "hash.value=%s\n", hexdigest);
    }
    else { // default to md5
        MD5_Final(digest, &md5_ctx);
        for (n = 0; n < MD5_DIGEST_LENGTH; ++n) {
            sprintf(hexdigest+2*n, "%02x", digest[n]);
        }
        p += sprintf(p, "hash.value=%s\n", hexdigest);
    }

    int rc = tar_append_file_contents(tar, "EOD", 0600,
            getuid(), getgid(), eodbuf, p-eodbuf);
    return rc;
}

static int do_backup_tree(const String8& path)
{
    int rc = 0;
    bool path_is_data = !strcmp(path.string(), "/data");
    DIR *dp;

    dp = opendir(path.string());
    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
            continue;
        }
        if (path_is_data && !strcmp(de->d_name, "media") && is_data_media()) {
            logmsg("do_backup_tree: skipping datamedia\n");
            continue;
        }
        struct stat st;
        String8 filepath(path);
        filepath += "/";
        filepath += de->d_name;

        memset(&st, 0, sizeof(st));
        rc = lstat(filepath.string(), &st);
        if (rc != 0) {
            logmsg("do_backup_tree: path=%s, lstat failed, rc=%d\n", path.string(), rc);
            break;
        }

        if (!(S_ISREG(st.st_mode) || S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode))) {
            logmsg("do_backup_tree: path=%s, ignoring special file\n", path.string());
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            rc = tar_append_file(tar, filepath.string(), filepath.string());
            if (rc != 0) {
                logmsg("do_backup_tree: path=%s, tar_append_file failed, rc=%d\n", path.string(), rc);
                break;
            }
            rc = do_backup_tree(filepath);
            if (rc != 0) {
                logmsg("do_backup_tree: path=%s, recursion failed, rc=%d\n", path.string(), rc);
                break;
            }
        }
        else {
            rc = tar_append_file(tar, filepath.string(), filepath.string());
            if (rc != 0) {
                logmsg("do_backup_tree: path=%s, tar_append_file failed, rc=%d\n", path.string(), rc);
                break;
            }
        }
    }
    closedir(dp);
    return rc;
}

static int tar_append_device_contents(TAR* t, const char* devname, const char* savename)
{
    struct stat st;
    memset(&st, 0, sizeof(st));
    if (lstat(devname, &st) != 0) {
        logmsg("tar_append_device_contents: lstat %s failed\n", devname);
        return -1;
    }
    st.st_mode = 0644 | S_IFREG;

    int fd = open(devname, O_RDONLY);
    if (fd < 0) {
        logmsg("tar_append_device_contents: open %s failed\n", devname);
        return -1;
    }
    st.st_size = lseek(fd, 0, SEEK_END);
    close(fd);

    th_set_from_stat(t, &st);
    th_set_path(t, savename);
    if (th_write(t) != 0) {
        logmsg("tar_append_device_contents: th_write failed\n");
        return -1;
    }
    if (tar_append_regfile(t, devname) != 0) {
        logmsg("tar_append_device_contents: tar_append_regfile %s failed\n", devname);
        return -1;
    }
    return 0;
}

int do_backup(int argc, char **argv)
{
    int rc = 1;
    int n;
    int i;

    int len;
    int written;

    const char* opt_compress = "gzip";
    const char* opt_hash = "md5";

    int optidx = 0;
    while (optidx < argc && argv[optidx][0] == '-' && argv[optidx][1] == '-') {
        char* optname = &argv[optidx][2];
        ++optidx;
        char* optval = strchr(optname, '=');
        if (optval) {
            *optval = '\0';
            ++optval;
        }
        else {
            if (optidx >= argc) {
                logmsg("No argument to --%s\n", optname);
                return -1;
            }
            optval = argv[optidx];
            ++optidx;
        }
        if (!strcmp(optname, "compress")) {
            opt_compress = optval;
            logmsg("do_backup: compress=%s\n", opt_compress);
        }
        else if (!strcmp(optname, "hash")) {
            opt_hash = optval;
            logmsg("do_backup: hash=%s\n", opt_hash);
        }
        else {
            logmsg("do_backup: invalid option name \"%s\"\n", optname);
            return -1;
        }
    }
    for (n = optidx; n < argc; ++n) {
        const char* partname = argv[n];
        if (*partname == '-')
            ++partname;
        if (part_add(partname) != 0) {
            logmsg("Failed to add partition %s\n", partname);
            return -1;
        }
    }

    rc = create_tar(opt_compress, "w");
    if (rc != 0) {
        logmsg("do_backup: cannot open tar stream\n");
        return rc;
    }

    append_sod(opt_hash);

    hash_name = strdup(opt_hash);

    for (i = 0; i < MAX_PART; ++i) {
        partspec* curpart = part_get(i);
        if (!curpart)
            break;

        part_set(curpart);
        const char* fstype = curpart->vol->fs_type;
        if (!strcmp(fstype, "mtd") || !strcmp(fstype, "bml") || !strcmp(fstype, "emmc")) {
            rc = tar_append_device_contents(tar, curpart->vol->blk_device, curpart->name);
        }
        else {
            if (ensure_path_mounted(curpart->path) != 0) {
                logmsg("do_backup: cannot mount %s\n", curpart->path);
                continue;
            }
            String8 path(curpart->path);
            rc = do_backup_tree(path);
            ensure_path_unmounted(curpart->path);
        }
    }

    free(hash_name);
    hash_name = NULL;

    append_eod(opt_hash);

    tar_append_eof(tar);

    if (opt_compress)
        gzflush(gzf, Z_FINISH);

    logmsg("backup complete: rc=%d\n", rc);

    return rc;
}

