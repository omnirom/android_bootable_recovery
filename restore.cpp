#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/vfs.h>

#include <cutils/properties.h>

#include <lib/libtar.h>
#include <zlib.h>

#include "roots.h"

#include "bu.h"

#include "messagesocket.h"

using namespace android;

static int verify_sod()
{
    int fd;
    const char* key;
    char value[PROPERTY_VALUE_MAX];
    char sodbuf[PROP_LINE_LEN*10];

    fd = open(PATHNAME_SOD, O_RDONLY);
    if (fd < 0) {
        logmsg("tar_verify_sod: failed to open file\n");
        return -1;
    }

    ssize_t len = read(fd, sodbuf, sizeof(sodbuf));
    if (len <= 0) {
        logmsg("verify_sod: short read\n");
        close(fd);
        return -1;
    }

    char val_hashname[PROPERTY_VALUE_MAX];
    memset(val_hashname, 0, sizeof(val_hashname));
    char val_product[PROPERTY_VALUE_MAX];
    memset(val_product, 0, sizeof(val_product));
    char* p = sodbuf;
    char* q;
    while ((q = strchr(p, '\n')) != NULL) {
        char* key = p;
        *q = '\0';
        logmsg("verify_sod: line=%s\n", p);
        p = q+1;
        char* val = strchr(key, '=');
        if (val) {
            *val = '\0';
            ++val;
            if (strcmp(key, "hash.name") == 0) {
                strncpy(val_hashname, val, sizeof(val_hashname));
            }
            if (strcmp(key, "ro.build.product") == 0) {
                strncpy(val_product, val, sizeof(val_product));
            }
        }
    }
    close(fd);

    if (!val_hashname[0]) {
        logmsg("verify_sod: did not find hash.name\n");
        return -1;
    }
    hash_name = strdup(val_hashname);

    if (!val_product[0]) {
        logmsg("verify_sod: did not find ro.build.product\n");
        return -1;
    }
    key = "ro.build.product";
    property_get(key, value, "");
    if (strcmp(val_product, value) != 0) {
        logmsg("verify_sod: product does not match\n");
        return -1;
    }

    return 0;
}

static int verify_eod(size_t actual_hash_datalen,
        SHA_CTX* actual_sha_ctx, MD5_CTX* actual_md5_ctx)
{
    int rc = -1;
    char eodbuf[PROP_LINE_LEN*10];

    int fd = open(PATHNAME_EOD, O_RDONLY);
    if (fd < 0) {
        logmsg("verify_eod: failed to open file\n");
        return -1;
    }
    int len = read(fd, eodbuf, sizeof(eodbuf));
    if (len <= 0) {
        logmsg("tar_verify_sod: short read\n");
        close(fd);
        return -1;
    }

    size_t reported_datalen = 0;
    char reported_hash[HASH_MAX_STRING_LENGTH];
    memset(reported_hash, 0, sizeof(reported_hash));
    char* p = eodbuf;
    char* q;
    while ((q = strchr(p, '\n')) != NULL) {
        char* key = p;
        *q = '\0';
        logmsg("verify_eod: line=%s\n", p);
        p = q+1;
        char* val = strchr(key, '=');
        if (val) {
            *val = '\0';
            ++val;
            if (strcmp(key, "hash.datalen") == 0) {
                reported_datalen = strtoul(val, NULL, 0);
            }
            if (strcmp(key, "hash.value") == 0) {
                memset(reported_hash, 0, sizeof(reported_hash));
                strncpy(reported_hash, val, sizeof(reported_hash));
            }
        }
    }

    unsigned char digest[HASH_MAX_LENGTH];
    char hexdigest[HASH_MAX_STRING_LENGTH];

    int n;
    if (hash_name != NULL && !strcasecmp(hash_name, "sha1")) {
        SHA1_Final(digest, actual_sha_ctx);
         for (n = 0; n < SHA_DIGEST_LENGTH; ++n) {
             sprintf(hexdigest+2*n, "%02x", digest[n]);
         }
    }
    else { // default to md5
        MD5_Final(digest, actual_md5_ctx);
        for (n = 0; n < MD5_DIGEST_LENGTH; ++n) {
            sprintf(hexdigest+2*n, "%02x", digest[n]);
        }
    }

    logmsg("verify_eod: expected=%d,%s\n", actual_hash_datalen, hexdigest);

    logmsg("verify_eod: reported=%d,%s\n", reported_datalen, reported_hash);

    if ((reported_datalen == actual_hash_datalen) &&
            (memcmp(hexdigest, reported_hash, strlen(hexdigest)) == 0)) {
        rc = 0;
    }

    close(fd);

    return rc;
}

static int do_restore_tree(int sockfd)
{
    int rc = 0;
    ssize_t len;
    const char* compress = "none";
    char buf[512];
    char rootpath[] = "/";

    logmsg("do_restore_tree: enter\n");

    len = recv(sockfd, buf, sizeof(buf), MSG_PEEK);
    if (len < 0) {
        logmsg("do_restore_tree: peek(%d) failed (%d:%s)\n", sockfd, rc, strerror(errno));
        return -1;
    }
    if (len < 2) {
        logmsg("do_restore_tree: peek returned %d\n", len);
        return -1;
    }
    if (buf[0] == 0x1f && buf[1] == 0x8b) {
        logmsg("do_restore_tree: is gzip\n");
        compress = "gzip";
    }

    create_tar(compress, "r");

    size_t save_hash_datalen;
    SHA_CTX save_sha_ctx;
    MD5_CTX save_md5_ctx;

    char cur_mount[PATH_MAX];
    cur_mount[0] = '\0';
    while (1) {
        save_hash_datalen = hash_datalen;
        memcpy(&save_sha_ctx, &sha_ctx, sizeof(SHA_CTX));
        memcpy(&save_md5_ctx, &md5_ctx, sizeof(MD5_CTX));
        rc = th_read(tar);
        if (rc != 0) {
            if (rc == 1) { // EOF
                rc = 0;
            }
            break;
        }
        char* pathname = th_get_pathname(tar);
        logmsg("do_restore_tree: extract %s\n", pathname);
        if (pathname[0] == '/') {
            const char* mntend = strchr(&pathname[1], '/');
            if (!mntend) {
                mntend = pathname + strlen(pathname);
            }
            if (memcmp(cur_mount, pathname, mntend-pathname) != 0) {
                // New mount
                if (cur_mount[0]) {
                    logmsg("do_restore_tree: unmounting %s\n", cur_mount);
                    ensure_path_unmounted(cur_mount);
                }
                memcpy(cur_mount, pathname, mntend-pathname);
                cur_mount[mntend-pathname] = '\0';

                // XXX: Assume paths are not interspersed
                logmsg("do_restore_tree: switching to %s\n", cur_mount);
                rc = ensure_path_unmounted(cur_mount);
                if (rc != 0) {
                    logmsg("do_restore_tree: cannot unmount %s\n", cur_mount);
                    break;
                }
                logmsg("do_restore_tree: formatting %s\n", cur_mount);
                rc = format_volume(cur_mount);
                if (rc != 0) {
                    logmsg("do_restore_tree: cannot format %s\n", cur_mount);
                    break;
                }
                rc = ensure_path_mounted(cur_mount);
                if (rc != 0) {
                    logmsg("do_restore_tree: cannot mount %s\n", cur_mount);
                    break;
                }
            }
        }
        if (!strcmp(pathname, "SOD")) {
            rc = tar_extract_file(tar, PATHNAME_SOD);
            if (rc == 0) {
                rc = verify_sod();
            }
            logmsg("do_restore_tree: tar_verify_sod returned %d\n", rc);
        }
        else if (!strcmp(pathname, "EOD")) {
            rc = tar_extract_file(tar, PATHNAME_EOD);
            if (rc == 0) {
                rc = verify_eod(save_hash_datalen, &save_sha_ctx, &save_md5_ctx);
            }
            logmsg("do_restore_tree: tar_verify_eod returned %d\n", rc);
        }
        else if (!strcmp(pathname, "boot") || !strcmp(pathname, "recovery")) {
            char mnt[20];
            sprintf(mnt, "/%s", pathname);
            fstab_rec* vol = volume_for_path(mnt);
            if (vol != NULL && vol->fs_type != NULL) {
                rc = tar_extract_file(tar, vol->blk_device);
            }
            else {
                logmsg("do_restore_tree: cannot find volume for %s\n", mnt);
            }
        }
        else {
            rc = tar_extract_file(tar, pathname);
        }
        free(pathname);
        if (rc != 0) {
            logmsg("extract failed, rc=%d\n", rc);
            break;
        }
    }

    if (cur_mount[0]) {
        logmsg("do_restore_tree: unmounting %s\n", cur_mount);
        ensure_path_unmounted(cur_mount);
    }

    tar_close(tar);

    return rc;
}

int do_restore(int argc, char **argv)
{
    int rc = 1;
    int n;

    char buf[256];
    int len;
    int written;

    MessageSocket ms;
    ms.ClientInit();
    ms.Show("Restore in progress...");

    rc = do_restore_tree(sockfd);
    logmsg("do_restore: rc=%d\n", rc);

    ms.Dismiss();

    free(hash_name);
    hash_name = NULL;

    return rc;
}

