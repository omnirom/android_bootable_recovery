/*
**  Copyright 1998-2003 University of Illinois Board of Trustees
**  Copyright 1998-2003 Mark D. Roth
**  All rights reserved.
**
**  wrapper.c - libtar high-level wrapper code
**
**  Mark D. Roth <roth@uiuc.edu>
**  Campus Information Technologies and Educational Services
**  University of Illinois at Urbana-Champaign
*/

#define DEBUG
#include <internal.h>

#include <stdio.h>
#include <sys/param.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#ifdef STDC_HEADERS
# include <string.h>
#endif

int
tar_extract_glob(TAR *t, char *globname, char *prefix)
{
	char *filename;
	char buf[MAXPATHLEN];
	int i;

	while ((i = th_read(t)) == 0)
	{
		filename = th_get_pathname(t);
		if (fnmatch(globname, filename, FNM_PATHNAME | FNM_PERIOD))
		{
			if (TH_ISREG(t) && tar_skip_regfile(t))
				return -1;
			continue;
		}
		if (t->options & TAR_VERBOSE)
			th_print_long_ls(t);
		if (prefix != NULL)
			snprintf(buf, sizeof(buf), "%s/%s", prefix, filename);
		else
			strlcpy(buf, filename, sizeof(buf));
		if (tar_extract_file(t, filename, prefix) != 0)
			return -1;
	}

	return (i == 1 ? 0 : -1);
}


int
tar_extract_all(TAR *t, char *prefix)
{
	char *filename;
	char buf[MAXPATHLEN];
	int i;
	printf("prefix: %s\n", prefix);
#ifdef DEBUG
	printf("==> tar_extract_all(TAR *t, \"%s\")\n",
	       (prefix ? prefix : "(null)"));
#endif
	while ((i = th_read(t)) == 0)
	{
#ifdef DEBUG
		puts("    tar_extract_all(): calling th_get_pathname()");
#endif
		filename = th_get_pathname(t);
		if (t->options & TAR_VERBOSE)
			th_print_long_ls(t);
		if (prefix != NULL)
			snprintf(buf, sizeof(buf), "%s/%s", prefix, filename);
		else
			strlcpy(buf, filename, sizeof(buf));
#ifdef DEBUG
		printf("    tar_extract_all(): calling tar_extract_file(t, "
		       "\"%s\")\n", buf);
#endif
		printf("item name: '%s'\n", filename);
		/*
		if (strcmp(filename, "/") == 0) {
			printf("skipping /\n");
			continue;
		}
		*/
		if (tar_extract_file(t, buf, prefix) != 0)
			return -1;
	}
	return (i == 1 ? 0 : -1);
}


int
tar_append_tree(TAR *t, char *realdir, char *savedir, char *exclude)
{
#ifdef DEBUG
	printf("==> tar_append_tree(0x%lx, \"%s\", \"%s\")\n",
	       (long unsigned int)t, realdir, (savedir ? savedir : "[NULL]"));
#endif

	char temp[1024];
	int skip = 0, i, n_spaces = 0;
	char ** excluded = NULL;
	char * p = NULL;
	if (exclude) {
		strcpy(temp, exclude);
		p = strtok(exclude, " ");
		if (p == NULL) {
			excluded = realloc(excluded, sizeof(char*) * (++n_spaces));
			excluded[0] = temp;
		} else {
			while (p) {
				excluded = realloc(excluded, sizeof(char*) * (++n_spaces));
				excluded[n_spaces-1] = p;
				p = strtok(NULL, " ");
			}
		}
		excluded = realloc(excluded, sizeof(char*) * (n_spaces+1));
		excluded[n_spaces] = 0;
		for (i = 0; i < (n_spaces+1); i++) {
			if (realdir == excluded[i]) {
				printf("    excluding '%s'\n", excluded[i]);
				skip = 1;
				break;
			}
		}
	}
	if (skip == 0) {
		if (tar_append_file(t, realdir, savedir) != 0)
			return -1;
	}

	char realpath[MAXPATHLEN];
	char savepath[MAXPATHLEN];
	struct dirent *dent;
	DIR *dp;
	struct stat s;

	dp = opendir(realdir);
	if (dp == NULL) {
		if (errno == ENOTDIR)
			return 0;
		return -1;
	}
	while ((dent = readdir(dp)) != NULL) {
		if(strcmp(dent->d_name, ".") == 0
		|| strcmp(dent->d_name, "..") == 0)
			continue;

		if (exclude) {
			int omit = 0;
			for (i = 0; i < (n_spaces+1); i++) {
				if (excluded[i] != NULL) {
						if (strcmp(dent->d_name, excluded[i]) == 0 || strcmp(excluded[i], realdir) == 0) {
							printf("    excluding '%s'\n", excluded[i]);
							omit = 1;
							break;
						}
				}
			}
			if (omit)
				continue;
		}

		snprintf(realpath, MAXPATHLEN, "%s/%s", realdir, dent->d_name);
		if (savedir)
			snprintf(savepath, MAXPATHLEN, "%s/%s", savedir, dent->d_name);

		if (lstat(realpath, &s) != 0)
			return -1;

		if (S_ISDIR(s.st_mode)) {
			if (tar_append_tree(t, realpath, (savedir ? savepath : NULL), (exclude ? exclude : NULL)) != 0)
				return -1;
			continue;
		} else {
			if (tar_append_file(t, realpath, (savedir ? savepath : NULL)) != 0)
				return -1;
			continue;
		}
	}
	closedir(dp);
	free(excluded);

	return 0;
}


int
tar_find(TAR *t, char *searchstr)
{
	if (!searchstr)
		return 0;

	char *filename;
	int i, entryfound = 0;
#ifdef DEBUG
	printf("==> tar_find(0x%lx, %s)\n", (long unsigned int)t, searchstr);
#endif
	while ((i = th_read(t)) == 0) {
		filename = th_get_pathname(t);
		if (fnmatch(searchstr, filename, FNM_FILE_NAME | FNM_PERIOD) == 0) {
			entryfound++;
#ifdef DEBUG
			printf("Found matching entry: %s\n", filename);
#endif
			break;
		}
	}
#ifdef DEBUG
	if (!entryfound)
		printf("No matching entry found.\n");
#endif

	return entryfound;
}
