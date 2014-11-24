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
#include <cutils/log.h>

#include <selinux/label.h>

#include "roots.h"

#include "bu.h"

#include "messagesocket.h"

#define PATHNAME_RC "/tmp/burc"

using namespace android;

struct selabel_handle *sehandle;

int sockfd;
TAR* tar;
gzFile gzf;

char* hash_name;
size_t hash_datalen;
SHA_CTX sha_ctx;
MD5_CTX md5_ctx;

static MessageSocket ms;

void
ui_print(const char* format, ...) {
    char buffer[256];

    va_list ap;
    va_start(ap, format);
    vsnprintf(buffer, sizeof(buffer), format, ap);
    va_end(ap);

    fputs(buffer, stdout);
}

void logmsg(const char *fmt, ...)
{
    char msg[1024];
    FILE* fp;
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    fp = fopen("/tmp/bu.log", "a");
    if (fp) {
        fprintf(fp, "[%d] %s", getpid(), msg);
        fclose(fp);
    }
}

static partspec partlist[MAX_PART];
static partspec* curpart;

int part_add(const char* name)
{
    fstab_rec* vol = NULL;
    char* path = NULL;
    int i;

    path = (char*)malloc(1+strlen(name)+1);
    sprintf(path, "/%s", name);
    vol = volume_for_path(path);
    if (vol == NULL || vol->fs_type == NULL) {
        logmsg("missing vol info for %s, ignoring\n", name);
        goto err;
    }

    for (i = 0; i < MAX_PART; ++i) {
        if (partlist[i].name == NULL) {
            partlist[i].name = strdup(name);
            partlist[i].path = path;
            partlist[i].vol = vol;
            logmsg("part_add: i=%d, name=%s, path=%s\n", i, name, path);
            return 0;
        }
        if (strcmp(partlist[i].name, name) == 0) {
            logmsg("duplicate partition %s, ignoring\n", name);
            goto err;
        }
    }

err:
    free(path);
    return -1;
}

partspec* part_get(int i)
{
    if (i >= 0 && i < MAX_PART) {
        if (partlist[i].name != NULL) {
            return &partlist[i];
        }
    }
    return NULL;
}

partspec* part_find(const char* name)
{
    for (int i = 0; i < MAX_PART; ++i) {
        if (partlist[i].name && !strcmp(name, partlist[i].name)) {
            return &partlist[i];
        }
    }
    return NULL;
}

void part_set(partspec* part)
{
    curpart = part;
    curpart->off = 0;
}

int update_progress(uint64_t off)
{
    if (curpart) {
        int oldpct = min(100, (int)((uint64_t)100*curpart->off/curpart->used));
        curpart->off += off;
        int newpct = min(100, (int)((uint64_t)100*curpart->off/curpart->used));
        if (newpct > oldpct) {
            char msg[256];
            sprintf(msg, "%s: %d%% complete", curpart->name, newpct);
            ms.Show(msg);
        }
    }
    return 0;
}

static int tar_cb_open(const char* path, int mode, ...)
{
    errno = EINVAL;
    return -1;
}

static int tar_cb_close(int fd)
{
    return 0;
}

static ssize_t tar_cb_read(int fd, void* buf, size_t len)
{
    ssize_t nread;
    nread = ::read(fd, buf, len);
    if (nread > 0 && hash_name) {
        SHA1_Update(&sha_ctx, (u_char*)buf, nread);
        MD5_Update(&md5_ctx, buf, nread);
        hash_datalen += nread;
    }
    update_progress(nread);
    return nread;
}

static ssize_t tar_cb_write(int fd, const void* buf, size_t len)
{
    ssize_t written = 0;

    if (hash_name) {
        SHA1_Update(&sha_ctx, (u_char*)buf, len);
        MD5_Update(&md5_ctx, buf, len);
        hash_datalen += len;
    }

    while (len > 0) {
        ssize_t n = ::write(fd, buf, len);
        if (n < 0) {
            logmsg("tar_cb_write: error: n=%d\n", n);
            return n;
        }
        if (n == 0)
            break;
        buf = (const char *)buf + n;
        len -= n;
        written += n;
    }
    update_progress(written);
    return written;
}

static tartype_t tar_io = {
    tar_cb_open,
    tar_cb_close,
    tar_cb_read,
    tar_cb_write
};

static ssize_t tar_gz_cb_read(int fd, void* buf, size_t len)
{
    int nread;
    nread = gzread(gzf, buf, len);
    if (nread > 0 && hash_name) {
        SHA1_Update(&sha_ctx, (u_char*)buf, nread);
        MD5_Update(&md5_ctx, buf, nread);
        hash_datalen += nread;
    }
    update_progress(nread);
    return nread;
}

static ssize_t tar_gz_cb_write(int fd, const void* buf, size_t len)
{
    ssize_t written = 0;

    if (hash_name) {
        SHA1_Update(&sha_ctx, (u_char*)buf, len);
        MD5_Update(&md5_ctx, buf, len);
        hash_datalen += len;
    }

    while (len > 0) {
        ssize_t n = gzwrite(gzf, buf, len);
        if (n < 0) {
            logmsg("tar_gz_cb_write: error: n=%d\n", n);
            return n;
        }
        if (n == 0)
            break;
        buf = (const char *)buf + n;
        len -= n;
        written += n;
    }
    update_progress(written);
    return written;
}

static tartype_t tar_io_gz = {
    tar_cb_open,
    tar_cb_close,
    tar_gz_cb_read,
    tar_gz_cb_write
};

int create_tar(const char* compress, const char* mode)
{
    int rc = -1;

    SHA1_Init(&sha_ctx);
    MD5_Init(&md5_ctx);

    if (!compress || strcasecmp(compress, "none") == 0) {
        rc = tar_fdopen(&tar, sockfd, "foobar", &tar_io,
                0, /* oflags: unused */
                0, /* mode: unused */
                TAR_GNU | TAR_STORE_SELINUX /* options */);
    }
    else if (strcasecmp(compress, "gzip") == 0) {
        gzf = gzdopen(sockfd, mode);
        if (gzf != NULL) {
            rc = tar_fdopen(&tar, 0, "foobar", &tar_io_gz,
                    0, /* oflags: unused */
                    0, /* mode: unused */
                    TAR_GNU | TAR_STORE_SELINUX /* options */);
        }
    }
    return rc;
}

static void do_exit(int rc)
{
    char rcstr[80];
    int len;
    len = sprintf(rcstr, "%d\n", rc);

    unlink(PATHNAME_RC);
    int fd = open(PATHNAME_RC, O_RDWR|O_CREAT, 0644);
    write(fd, rcstr, len);
    close(fd);
    exit(rc);
}

int main(int argc, char **argv)
{
    int n;
    int rc = 1;

    logmsg("bu: invoked with %d args\n", argc);

    if (argc < 3) {
        logmsg("Not enough args (%d)\n", argc);
        do_exit(1);
    }

    // progname sockfd args...
    int optidx = 1;
    sockfd = atoi(argv[optidx++]);
    const char* opname = argv[optidx++];

    struct selinux_opt seopts[] = {
      { SELABEL_OPT_PATH, "/file_contexts" }
    };
    sehandle = selabel_open(SELABEL_CTX_FILE, seopts, 1);

    load_volume_table();
//    vold_client_start(&v_callbacks, 1);

    ms.ClientInit();

    if (!strcmp(opname, "backup")) {
        ms.Show("Backup in progress...");
        rc = do_backup(argc-optidx, &argv[optidx]);
    }
    else if (!strcmp(opname, "restore")) {
        ms.Show("Restore in progress...");
        rc = do_restore(argc-optidx, &argv[optidx]);
    }
    else {
        logmsg("Unknown operation %s\n", opname);
        do_exit(1);
    }

    ms.Dismiss();

    sleep(1);
    close(sockfd);

    logmsg("bu exiting\n");

    do_exit(rc);

    return rc;
}
