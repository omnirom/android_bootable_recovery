#include <dirent.h>
#include <errno.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <selinux/selinux.h>

// simple dynamic array of strings.
typedef struct {
    int count;
    int capacity;
    void** items;
} strlist_t;

#define STRLIST_INITIALIZER { 0, 0, NULL }

/* Used to iterate over a strlist_t
 * _list   :: pointer to strlist_t object
 * _item   :: name of local variable name defined within the loop with
 *            type 'char*'
 * _stmnt  :: C statement executed in each iteration
 *
 * This macro is only intended for simple uses. Do not add or remove items
 * to/from the list during iteration.
 */
#define  STRLIST_FOREACH(_list,_item,_stmnt) \
    do { \
        int _nn_##__LINE__ = 0; \
        for (;_nn_##__LINE__ < (_list)->count; ++ _nn_##__LINE__) { \
            char* _item = (char*)(_list)->items[_nn_##__LINE__]; \
            _stmnt; \
        } \
    } while (0)

static void dynarray_reserve_more( strlist_t *a, int count ) {
    int old_cap = a->capacity;
    int new_cap = old_cap;
    const int max_cap = INT_MAX/sizeof(void*);
    void** new_items;
    int new_count = a->count + count;

    if (count <= 0)
        return;

    if (count > max_cap - a->count)
        abort();

    new_count = a->count + count;

    while (new_cap < new_count) {
        old_cap = new_cap;
        new_cap += (new_cap >> 2) + 4;
        if (new_cap < old_cap || new_cap > max_cap) {
            new_cap = max_cap;
        }
    }
    new_items = realloc(a->items, new_cap*sizeof(void*));
    if (new_items == NULL)
        abort();

    a->items = new_items;
    a->capacity = new_cap;
}

void strlist_init( strlist_t *list ) {
    list->count = list->capacity = 0;
    list->items = NULL;
}

// append a new string made of the first 'slen' characters from 'str'
// followed by a trailing zero.
void strlist_append_b( strlist_t *list, const void* str, size_t  slen ) {
    char *copy = malloc(slen+1);
    memcpy(copy, str, slen);
    copy[slen] = '\0';
    if (list->count >= list->capacity)
        dynarray_reserve_more(list, 1);
    list->items[list->count++] = copy;
}

// append the copy of a given input string to a strlist_t.
void strlist_append_dup( strlist_t *list, const char *str) {
    strlist_append_b(list, str, strlen(str));
}

// note: strlist_done will free all the strings owned by the list.
void strlist_done( strlist_t *list ) {
    STRLIST_FOREACH(list, string, free(string));
    free(list->items);
    list->items = NULL;
    list->count = list->capacity = 0;
}

static int strlist_compare_strings(const void* a, const void* b) {
    const char *sa = *(const char **)a;
    const char *sb = *(const char **)b;
    return strcmp(sa, sb);
}

/* sort the strings in a given list (using strcmp) */
void strlist_sort( strlist_t *list ) {
    if (list->count > 0) {
        qsort(list->items, (size_t)list->count, sizeof(void*), strlist_compare_strings);
    }
}


// bits for flags argument
#define LIST_LONG           (1 << 0)
#define LIST_ALL            (1 << 1)
#define LIST_RECURSIVE      (1 << 2)
#define LIST_DIRECTORIES    (1 << 3)
#define LIST_SIZE           (1 << 4)
#define LIST_LONG_NUMERIC   (1 << 5)
#define LIST_CLASSIFY       (1 << 6)
#define LIST_MACLABEL       (1 << 7)
#define LIST_INODE          (1 << 8)

// fwd
static int listpath(const char *name, int flags);

static char mode2kind(mode_t mode)
{
    switch(mode & S_IFMT){
    case S_IFSOCK: return 's';
    case S_IFLNK: return 'l';
    case S_IFREG: return '-';
    case S_IFDIR: return 'd';
    case S_IFBLK: return 'b';
    case S_IFCHR: return 'c';
    case S_IFIFO: return 'p';
    default: return '?';
    }
}

void strmode(mode_t mode, char *out)
{
    *out++ = mode2kind(mode);

    *out++ = (mode & 0400) ? 'r' : '-';
    *out++ = (mode & 0200) ? 'w' : '-';
    if(mode & 04000) {
        *out++ = (mode & 0100) ? 's' : 'S';
    } else {
        *out++ = (mode & 0100) ? 'x' : '-';
    }
    *out++ = (mode & 040) ? 'r' : '-';
    *out++ = (mode & 020) ? 'w' : '-';
    if(mode & 02000) {
        *out++ = (mode & 010) ? 's' : 'S';
    } else {
        *out++ = (mode & 010) ? 'x' : '-';
    }
    *out++ = (mode & 04) ? 'r' : '-';
    *out++ = (mode & 02) ? 'w' : '-';
    if(mode & 01000) {
        *out++ = (mode & 01) ? 't' : 'T';
    } else {
        *out++ = (mode & 01) ? 'x' : '-';
    }
    *out = 0;
}

static void user2str(uid_t uid, char *out, size_t out_size)
{
    struct passwd *pw = getpwuid(uid);
    if(pw) {
        strlcpy(out, pw->pw_name, out_size);
    } else {
        snprintf(out, out_size, "%d", uid);
    }
}

static void group2str(gid_t gid, char *out, size_t out_size)
{
    struct group *gr = getgrgid(gid);
    if(gr) {
        strlcpy(out, gr->gr_name, out_size);
    } else {
        snprintf(out, out_size, "%d", gid);
    }
}

static int show_total_size(const char *dirname, DIR *d, int flags)
{
    struct dirent *de;
    char tmp[1024];
    struct stat s;
    int sum = 0;

    /* run through the directory and sum up the file block sizes */
    while ((de = readdir(d)) != 0) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;
        if (de->d_name[0] == '.' && (flags & LIST_ALL) == 0)
            continue;

        if (strcmp(dirname, "/") == 0)
            snprintf(tmp, sizeof(tmp), "/%s", de->d_name);
        else
            snprintf(tmp, sizeof(tmp), "%s/%s", dirname, de->d_name);

        if (lstat(tmp, &s) < 0) {
            fprintf(stderr, "stat failed on %s: %s\n", tmp, strerror(errno));
            rewinddir(d);
            return -1;
        }

        sum += s.st_blocks / 2;
    }

    printf("total %d\n", sum);
    rewinddir(d);
    return 0;
}

static int listfile_size(const char *path, const char *filename, struct stat *s,
                         int flags)
{
    if(!s || !path) {
        return -1;
    }

    /* blocks are 512 bytes, we want output to be KB */
    if ((flags & LIST_SIZE) != 0) {
        printf("%lld ", (long long)s->st_blocks / 2);
    }

    if ((flags & LIST_CLASSIFY) != 0) {
        char filetype = mode2kind(s->st_mode);
        if (filetype != 'l') {
            printf("%c ", filetype);
        } else {
            struct stat link_dest;
            if (!stat(path, &link_dest)) {
                printf("l%c ", mode2kind(link_dest.st_mode));
            } else {
                fprintf(stderr, "stat '%s' failed: %s\n", path, strerror(errno));
                printf("l? ");
            }
        }
    }

    printf("%s\n", filename);

    return 0;
}

static int listfile_long(const char *path, struct stat *s, int flags)
{
    char date[32];
    char mode[16];
    char user[32];
    char group[32];
    const char *name;

    if(!s || !path) {
        return -1;
    }

    /* name is anything after the final '/', or the whole path if none*/
    name = strrchr(path, '/');
    if(name == 0) {
        name = path;
    } else {
        name++;
    }

    strmode(s->st_mode, mode);
    if (flags & LIST_LONG_NUMERIC) {
        snprintf(user, sizeof(user), "%u", s->st_uid);
        snprintf(group, sizeof(group), "%u", s->st_gid);
    } else {
        user2str(s->st_uid, user, sizeof(user));
        group2str(s->st_gid, group, sizeof(group));
    }

    strftime(date, 32, "%Y-%m-%d %H:%M", localtime((const time_t*)&s->st_mtime));
    date[31] = 0;

// 12345678901234567890123456789012345678901234567890123456789012345678901234567890
// MMMMMMMM UUUUUUUU GGGGGGGGG XXXXXXXX YYYY-MM-DD HH:MM NAME (->LINK)

    switch(s->st_mode & S_IFMT) {
    case S_IFBLK:
    case S_IFCHR:
        printf("%s %-8s %-8s %3d, %3d %s %s\n",
               mode, user, group,
               major(s->st_rdev), minor(s->st_rdev),
               date, name);
        break;
    case S_IFREG:
        printf("%s %-8s %-8s %8lld %s %s\n",
               mode, user, group, (long long)s->st_size, date, name);
        break;
    case S_IFLNK: {
        char linkto[256];
        ssize_t len;

        len = readlink(path, linkto, 256);
        if(len < 0) return -1;

        if(len > 255) {
            linkto[252] = '.';
            linkto[253] = '.';
            linkto[254] = '.';
            linkto[255] = 0;
        } else {
            linkto[len] = 0;
        }

        printf("%s %-8s %-8s          %s %s -> %s\n",
               mode, user, group, date, name, linkto);
        break;
    }
    default:
        printf("%s %-8s %-8s          %s %s\n",
               mode, user, group, date, name);

    }
    return 0;
}

static int listfile_maclabel(const char *path, struct stat *s)
{
    char mode[16];
    char user[32];
    char group[32];
    char *maclabel = NULL;
    const char *name;

    if(!s || !path) {
        return -1;
    }

    /* name is anything after the final '/', or the whole path if none*/
    name = strrchr(path, '/');
    if(name == 0) {
        name = path;
    } else {
        name++;
    }

    lgetfilecon(path, &maclabel);
    if (!maclabel) {
        return -1;
    }

    strmode(s->st_mode, mode);
    user2str(s->st_uid, user, sizeof(user));
    group2str(s->st_gid, group, sizeof(group));

    switch(s->st_mode & S_IFMT) {
    case S_IFLNK: {
        char linkto[256];
        ssize_t len;

        len = readlink(path, linkto, sizeof(linkto));
        if(len < 0) return -1;

        if((size_t)len > sizeof(linkto)-1) {
            linkto[sizeof(linkto)-4] = '.';
            linkto[sizeof(linkto)-3] = '.';
            linkto[sizeof(linkto)-2] = '.';
            linkto[sizeof(linkto)-1] = 0;
        } else {
            linkto[len] = 0;
        }

        printf("%s %-8s %-8s          %s %s -> %s\n",
               mode, user, group, maclabel, name, linkto);
        break;
    }
    default:
        printf("%s %-8s %-8s          %s %s\n",
               mode, user, group, maclabel, name);

    }

    free(maclabel);

    return 0;
}

static int listfile(const char *dirname, const char *filename, int flags)
{
    struct stat s;

    if ((flags & (LIST_LONG | LIST_SIZE | LIST_CLASSIFY | LIST_MACLABEL | LIST_INODE)) == 0) {
        printf("%s\n", filename);
        return 0;
    }

    char tmp[4096];
    const char* pathname = filename;

    if (dirname != NULL) {
        snprintf(tmp, sizeof(tmp), "%s/%s", dirname, filename);
        pathname = tmp;
    } else {
        pathname = filename;
    }

    if(lstat(pathname, &s) < 0) {
        fprintf(stderr, "lstat '%s' failed: %s\n", pathname, strerror(errno));
        return -1;
    }

    if(flags & LIST_INODE) {
        printf("%8llu ", (unsigned long long)s.st_ino);
    }

    if ((flags & LIST_MACLABEL) != 0) {
        return listfile_maclabel(pathname, &s);
    } else if ((flags & LIST_LONG) != 0) {
        return listfile_long(pathname, &s, flags);
    } else /*((flags & LIST_SIZE) != 0)*/ {
        return listfile_size(pathname, filename, &s, flags);
    }
}

static int listdir(const char *name, int flags)
{
    char tmp[4096];
    DIR *d;
    struct dirent *de;
    strlist_t  files = STRLIST_INITIALIZER;

    d = opendir(name);
    if(d == 0) {
        fprintf(stderr, "opendir failed, %s\n", strerror(errno));
        return -1;
    }

    if ((flags & LIST_SIZE) != 0) {
        show_total_size(name, d, flags);
    }

    while((de = readdir(d)) != 0){
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        if(de->d_name[0] == '.' && (flags & LIST_ALL) == 0) continue;

        strlist_append_dup(&files, de->d_name);
    }

    strlist_sort(&files);
    STRLIST_FOREACH(&files, filename, listfile(name, filename, flags));
    strlist_done(&files);

    if (flags & LIST_RECURSIVE) {
        strlist_t subdirs = STRLIST_INITIALIZER;

        rewinddir(d);

        while ((de = readdir(d)) != 0) {
            struct stat s;
            int err;

            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
                continue;
            if (de->d_name[0] == '.' && (flags & LIST_ALL) == 0)
                continue;

            if (!strcmp(name, "/"))
                snprintf(tmp, sizeof(tmp), "/%s", de->d_name);
            else
                snprintf(tmp, sizeof(tmp), "%s/%s", name, de->d_name);

            /*
             * If the name ends in a '/', use stat() so we treat it like a
             * directory even if it's a symlink.
             */
            if (tmp[strlen(tmp)-1] == '/')
                err = stat(tmp, &s);
            else
                err = lstat(tmp, &s);

            if (err < 0) {
                perror(tmp);
                closedir(d);
                return -1;
            }

            if (S_ISDIR(s.st_mode)) {
                strlist_append_dup(&subdirs, tmp);
            }
        }
        strlist_sort(&subdirs);
        STRLIST_FOREACH(&subdirs, path, {
            printf("\n%s:\n", path);
            listdir(path, flags);
        });
        strlist_done(&subdirs);
    }

    closedir(d);
    return 0;
}

static int listpath(const char *name, int flags)
{
    struct stat s;
    int err;

    /*
     * If the name ends in a '/', use stat() so we treat it like a
     * directory even if it's a symlink.
     */
    if (name[strlen(name)-1] == '/')
        err = stat(name, &s);
    else
        err = lstat(name, &s);

    if (err < 0) {
        perror(name);
        return -1;
    }

    if ((flags & LIST_DIRECTORIES) == 0 && S_ISDIR(s.st_mode)) {
        if (flags & LIST_RECURSIVE)
            printf("\n%s:\n", name);
        return listdir(name, flags);
    } else {
        /* yeah this calls stat() again*/
        return listfile(NULL, name, flags);
    }
}

int ls_main(int argc, char **argv)
{
    int flags = 0;

    if(argc > 1) {
        int i;
        int err = 0;
        strlist_t  files = STRLIST_INITIALIZER;

        for (i = 1; i < argc; i++) {
            if (argv[i][0] == '-') {
                /* an option ? */
                const char *arg = argv[i]+1;
                while (arg[0]) {
                    switch (arg[0]) {
                    case 'l': flags |= LIST_LONG; break;
                    case 'n': flags |= LIST_LONG | LIST_LONG_NUMERIC; break;
                    case 's': flags |= LIST_SIZE; break;
                    case 'R': flags |= LIST_RECURSIVE; break;
                    case 'd': flags |= LIST_DIRECTORIES; break;
                    case 'Z': flags |= LIST_MACLABEL; break;
                    case 'a': flags |= LIST_ALL; break;
                    case 'F': flags |= LIST_CLASSIFY; break;
                    case 'i': flags |= LIST_INODE; break;
                    default:
                        fprintf(stderr, "%s: Unknown option '-%c'. Aborting.\n", "ls", arg[0]);
                        exit(1);
                    }
                    arg++;
                }
            } else {
                /* not an option ? */
                strlist_append_dup(&files, argv[i]);
            }
        }

        if (files.count > 0) {
            STRLIST_FOREACH(&files, path, {
                if (listpath(path, flags) != 0) {
                    err = EXIT_FAILURE;
                }
            });
            strlist_done(&files);
            return err;
        }
    }

    // list working directory if no files or directories were specified
    return listpath(".", flags);
}
