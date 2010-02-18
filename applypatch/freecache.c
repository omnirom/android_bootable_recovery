#include <errno.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <unistd.h>
#include <dirent.h>
#include <ctype.h>

#include "applypatch.h"

static int EliminateOpenFiles(char** files, int file_count) {
  DIR* d;
  struct dirent* de;
  d = opendir("/proc");
  if (d == NULL) {
    printf("error opening /proc: %s\n", strerror(errno));
    return -1;
  }
  while ((de = readdir(d)) != 0) {
    int i;
    for (i = 0; de->d_name[i] != '\0' && isdigit(de->d_name[i]); ++i);
    if (de->d_name[i]) continue;

    // de->d_name[i] is numeric

    char path[FILENAME_MAX];
    strcpy(path, "/proc/");
    strcat(path, de->d_name);
    strcat(path, "/fd/");

    DIR* fdd;
    struct dirent* fdde;
    fdd = opendir(path);
    if (fdd == NULL) {
      printf("error opening %s: %s\n", path, strerror(errno));
      continue;
    }
    while ((fdde = readdir(fdd)) != 0) {
      char fd_path[FILENAME_MAX];
      char link[FILENAME_MAX];
      strcpy(fd_path, path);
      strcat(fd_path, fdde->d_name);

      int count;
      count = readlink(fd_path, link, sizeof(link)-1);
      if (count >= 0) {
        link[count] = '\0';

        // This is inefficient, but it should only matter if there are
        // lots of files in /cache, and lots of them are open (neither
        // of which should be true, especially in recovery).
        if (strncmp(link, "/cache/", 7) == 0) {
          int j;
          for (j = 0; j < file_count; ++j) {
            if (files[j] && strcmp(files[j], link) == 0) {
              printf("%s is open by %s\n", link, de->d_name);
              free(files[j]);
              files[j] = NULL;
            }
          }
        }
      }
    }
    closedir(fdd);
  }
  closedir(d);

  return 0;
}

int FindExpendableFiles(char*** names, int* entries) {
  DIR* d;
  struct dirent* de;
  int size = 32;
  *entries = 0;
  *names = malloc(size * sizeof(char*));

  char path[FILENAME_MAX];

  // We're allowed to delete unopened regular files in any of these
  // directories.
  const char* dirs[2] = {"/cache", "/cache/recovery/otatest"};

  unsigned int i;
  for (i = 0; i < sizeof(dirs)/sizeof(dirs[0]); ++i) {
    d = opendir(dirs[i]);
    if (d == NULL) {
      printf("error opening %s: %s\n", dirs[i], strerror(errno));
      continue;
    }

    // Look for regular files in the directory (not in any subdirectories).
    while ((de = readdir(d)) != 0) {
      strcpy(path, dirs[i]);
      strcat(path, "/");
      strcat(path, de->d_name);

      // We can't delete CACHE_TEMP_SOURCE; if it's there we might have
      // restarted during installation and could be depending on it to
      // be there.
      if (strcmp(path, CACHE_TEMP_SOURCE) == 0) continue;

      struct stat st;
      if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
        if (*entries >= size) {
          size *= 2;
          *names = realloc(*names, size * sizeof(char*));
        }
        (*names)[(*entries)++] = strdup(path);
      }
    }

    closedir(d);
  }

  printf("%d regular files in deletable directories\n", *entries);

  if (EliminateOpenFiles(*names, *entries) < 0) {
    return -1;
  }

  return 0;
}

int MakeFreeSpaceOnCache(size_t bytes_needed) {
  size_t free_now = FreeSpaceForFile("/cache");
  printf("%ld bytes free on /cache (%ld needed)\n",
         (long)free_now, (long)bytes_needed);

  if (free_now >= bytes_needed) {
    return 0;
  }

  char** names;
  int entries;

  if (FindExpendableFiles(&names, &entries) < 0) {
    return -1;
  }

  if (entries == 0) {
    // nothing we can delete to free up space!
    printf("no files can be deleted to free space on /cache\n");
    return -1;
  }

  // We could try to be smarter about which files to delete:  the
  // biggest ones?  the smallest ones that will free up enough space?
  // the oldest?  the newest?
  //
  // Instead, we'll be dumb.

  int i;
  for (i = 0; i < entries && free_now < bytes_needed; ++i) {
    if (names[i]) {
      unlink(names[i]);
      free_now = FreeSpaceForFile("/cache");
      printf("deleted %s; now %ld bytes free\n", names[i], (long)free_now);
      free(names[i]);
    }
  }

  for (; i < entries; ++i) {
    free(names[i]);
  }
  free(names);

  return (free_now >= bytes_needed) ? 0 : -1;
}
