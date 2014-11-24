#include <stdlib.h>
#include <stdio.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/vfs.h>

#include "cutils/properties.h"

#include "roots.h"

#include "bu.h"

#include "messagesocket.h"

using namespace android;

#define MAX_PART 8

struct partspec {
    const char* name;
    char*       path;
    fstab_rec*  vol;
};
struct partspec partlist[MAX_PART];

static int append_sod(const char* opt_hash)
{
    int fd;
    const char* key;
    char value[PROPERTY_VALUE_MAX];
    int len;
    char buf[PROP_LINE_LEN];

    unlink(PATHNAME_SOD);
    fd = open(PATHNAME_SOD, O_RDWR|O_CREAT, 0600);
    if (fd < 0) {
        return -1;
    }

    key = "hash.name";
    strcpy(value, opt_hash);
    len = sprintf(buf, "%s=%s\n", key, value);
    write(fd, buf, len);

    key = "ro.build.product";
    property_get(key, value, "");
    len = sprintf(buf, "%s=%s\n", key, value);
    write(fd, buf, len);

    for (int i = 0; partlist[i].name; ++i) {
        const char* fstype = partlist[i].vol->fs_type;
        uint64_t size, used;
        if (!strcmp(fstype, "mtd") || !strcmp(fstype, "bml") || !strcmp(fstype, "emmc")) {
            int fd = open(partlist[i].vol->blk_device, O_RDONLY);
            size = used = lseek(fd, 0, SEEK_END);
            close(fd);
        }
        else {
            struct statfs stfs;
            memset(&stfs, 0, sizeof(stfs));
            if (ensure_path_mounted(partlist[i].path) != 0) {
                logmsg("append_sod: failed to mount %s\n", partlist[i].path);
                continue;
            }
            if (statfs(partlist[i].path, &stfs) == 0) {
                size = (stfs.f_blocks) * stfs.f_bsize;
                used = (stfs.f_blocks - stfs.f_bfree) * stfs.f_bsize;
            }
            else {
                logmsg("Failed to statfs %s: %s\n", partlist[i].path, strerror(errno));
            }
            ensure_path_unmounted(partlist[i].path);
        }
        len = sprintf(buf, "fs.%s.size=%llu\n", partlist[i].name, size);
        write(fd, buf, len);
        len = sprintf(buf, "fs.%s.used=%llu\n", partlist[i].name, used);
        write(fd, buf, len);
    }

    close(fd);
    int rc = tar_append_file(tar, PATHNAME_SOD, "SOD");
    unlink(PATHNAME_SOD);
    return rc;
}

static int append_eod(const char* opt_hash)
{
    int fd;
    char buf[PROP_LINE_LEN];
    int len;
    int n;

    unlink(PATHNAME_EOD);
    fd = open(PATHNAME_EOD, O_RDWR|O_CREAT, 0600);
    if (fd < 0) {
        return -1;
    }

    len = sprintf(buf, "hash.datalen=%u\n", hash_datalen);
    write(fd, buf, len);

    unsigned char digest[HASH_MAX_LENGTH];
    char hexdigest[HASH_MAX_STRING_LENGTH];

    if (!strcasecmp(opt_hash, "sha1")) {
        SHA1_Final(digest, &sha_ctx);
        for (n = 0; n < SHA_DIGEST_LENGTH; ++n) {
            sprintf(hexdigest+2*n, "%02x", digest[n]);
        }
        len = sprintf(buf, "hash.value=%s\n", hexdigest);
    }
    else { // default to md5
        MD5_Final(digest, &md5_ctx);
        for (n = 0; n < MD5_DIGEST_LENGTH; ++n) {
            sprintf(hexdigest+2*n, "%02x", digest[n]);
        }
        len = sprintf(buf, "hash.value=%s\n", hexdigest);
    }

    write(fd, buf, len);

    close(fd);
    int rc = tar_append_file(tar, PATHNAME_EOD, "EOD");
    unlink(PATHNAME_EOD);
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
    if (argc - optidx >= MAX_PART) {
        logmsg("Too many partitions (%d > %d)\n", (argc-optidx), MAX_PART);
        return -1;
    }
    for (n = optidx; n < argc; ++n) {
        const char* partname = argv[n];
        if (*partname == '-')
            ++partname;
        for (i = 0; i < MAX_PART; ++i) {
            if (partlist[i].name == NULL) {
                partlist[i].name = partname;
                partlist[i].path = (char *)malloc(1+strlen(partname)+1);
                sprintf(partlist[i].path, "/%s", partname);
                partlist[i].vol = volume_for_path(partlist[i].path);
                if (partlist[i].vol == NULL || partlist[i].vol->fs_type == NULL) {
                    logmsg("do_backup: missing vol info for %s, ignoring\n", partname);
                    partlist[i].vol = NULL;
                    free(partlist[i].path);
                    partlist[i].path = NULL;
                    partlist[i].name = NULL;
                }
                break;
            }
            if (strcmp(partname, partlist[i].name) == 0) {
                logmsg("Ignoring duplicate partition %s\n", partname);
                break;
            }
        }
    }

    MessageSocket ms;
    ms.ClientInit();
    ms.Show("Backup in progress...");

    rc = create_tar(opt_compress, "w");
    if (rc != 0) {
        logmsg("do_backup: cannot open tar stream\n");
        return rc;
    }

    append_sod(opt_hash);

    hash_name = strdup(opt_hash);

    for (i = 0; partlist[i].name; ++i) {
        const char* fstype = partlist[i].vol->fs_type;
        if (!strcmp(fstype, "mtd") || !strcmp(fstype, "bml") || !strcmp(fstype, "emmc")) {
            rc = tar_append_device_contents(tar, partlist[i].vol->blk_device, partlist[i].name);
        }
        else {
            if (ensure_path_mounted(partlist[i].path) != 0) {
                logmsg("do_backup: cannot mount %s\n", partlist[i].path);
                continue;
            }
            String8 path(partlist[i].path);
            rc = do_backup_tree(path);
            ensure_path_unmounted(partlist[i].path);
        }

    }

    free(hash_name);
    hash_name = NULL;

    append_eod(opt_hash);

    tar_append_eof(tar);

    if (opt_compress)
        gzflush(gzf, Z_FINISH);

    ms.Dismiss();

    logmsg("backup complete: rc=%d\n", rc);

    return rc;
}

