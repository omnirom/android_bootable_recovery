#ifndef BB_ANDROID_SELINUX_H
#define BB_ANDROID_SELINUX_H

#include <selinux/selinux.h>
#include <selinux/label.h>
#include <selinux/android.h>

/* Set the function used by matchpathcon_init when displaying
   errors about the file_contexts configuration.  If not set,
   then this defaults to fprintf(stderr, fmt, ...). */
extern void set_matchpathcon_printf(void (*f) (const char *fmt, ...));

/* Set the function used by matchpathcon_init when checking the
   validity of a context in the file contexts configuration.  If not set,
   then this defaults to a test based on security_check_context().
   The function is also responsible for reporting any such error, and
   may include the 'path' and 'lineno' in such error messages. */
extern void set_matchpathcon_invalidcon(int (*f) (const char *path,
						  unsigned lineno,
						  char *context));

/* Same as above, but also allows canonicalization of the context,
   by changing *context to refer to the canonical form.  If not set,
   and invalidcon is also not set, then this defaults to calling
   security_canonicalize_context(). */
extern void set_matchpathcon_canoncon(int (*f) (const char *path,
						unsigned lineno,
						char **context));

/* Set flags controlling operation of matchpathcon_init or matchpathcon. */
#define MATCHPATHCON_BASEONLY 1	/* Only process the base file_contexts file. */
#define MATCHPATHCON_NOTRANS  2	/* Do not perform any context translation. */
#define MATCHPATHCON_VALIDATE 4	/* Validate/canonicalize contexts at init time. */
extern void set_matchpathcon_flags(unsigned int flags);

/* Load the file contexts configuration specified by 'path'
   into memory for use by subsequent matchpathcon calls.
   If 'path' is NULL, then load the active file contexts configuration,
   i.e. the path returned by selinux_file_context_path().
   Unless the MATCHPATHCON_BASEONLY flag has been set, this
   function also checks for a 'path'.homedirs file and
   a 'path'.local file and loads additional specifications
   from them if present. */
extern int matchpathcon_init(const char *path);

/* Same as matchpathcon_init, but only load entries with
   regexes that have stems that are prefixes of 'prefix'. */
extern int matchpathcon_init_prefix(const char *path, const char *prefix);

/* Free the memory allocated by matchpathcon_init. */
extern void matchpathcon_fini(void);

/* Resolve all of the symlinks and relative portions of a pathname, but NOT
 * the final component (same a realpath() unless the final component is a
 * symlink.  Resolved path must be a path of size PATH_MAX + 1 */
extern int realpath_not_final(const char *name, char *resolved_path);

/* Match the specified pathname and mode against the file contexts
   configuration and set *con to refer to the resulting context.
   'mode' can be 0 to disable mode matching.
   Caller must free via freecon.
   If matchpathcon_init has not already been called, then this function
   will call it upon its first invocation with a NULL path. */
extern int matchpathcon(const char *path,
			mode_t mode, char ** con);

/* Same as above, but return a specification index for
   later use in a matchpathcon_filespec_add() call - see below. */
extern int matchpathcon_index(const char *path,
			      mode_t mode, char ** con);

/* Maintain an association between an inode and a specification index,
   and check whether a conflicting specification is already associated
   with the same inode (e.g. due to multiple hard links).  If so, then
   use the latter of the two specifications based on their order in the
   file contexts configuration.  Return the used specification index. */
extern int matchpathcon_filespec_add(ino_t ino, int specind, const char *file);

/* Destroy any inode associations that have been added, e.g. to restart
   for a new filesystem. */
extern void matchpathcon_filespec_destroy(void);

/* Display statistics on the hash table usage for the associations. */
extern void matchpathcon_filespec_eval(void);

/* Check to see whether any specifications had no matches and report them.
   The 'str' is used as a prefix for any warning messages. */
extern void matchpathcon_checkmatches(char *str);

/*
 * Verify the context of the file 'path' against policy.
 * Return 1 if match, 0 if not and -1 on error.
 */
extern int selinux_file_context_verify(const char *path, mode_t mode);

/* Get the default security context for a user session for 'user'
   spawned by 'fromcon' and set *newcon to refer to it.  The context
   will be one of those authorized by the policy, but the selection
   of a default is subject to user customizable preferences.
   If 'fromcon' is NULL, defaults to current context.
   Returns 0 on success or -1 otherwise.
   Caller must free via freecon. */
extern int get_default_context(const char* user, const char* fromcon,
			char ** newcon);

/* Check a permission in the passwd class.
   Return 0 if granted or -1 otherwise. */
#define PASSWD__PASSWD  0x001UL
#define PASSWD__CHFN    0x002UL
#define PASSWD__CHSH    0x004UL
#define PASSWD__ROOTOK  0x008UL
#define PASSWD__CRONTAB 0x010UL
extern int selinux_check_passwd_access(access_vector_t requested);

#define lgetfilecon_raw(path, context) \
	lgetfilecon(path, context)

#define lsetfilecon_raw(path, scontext) \
	lsetfilecon(path, scontext)

#define selabel_lookup_raw(hnd, con, path, mode) \
	selabel_lookup(hnd, con, path, mode)

#define security_canonicalize_context_raw(context, newctx) \
	security_canonicalize_context(context, newctx)

#define getprevcon_raw(context) \
	getprevcon(context)

#define is_context_customizable(ctx) false

#define selinux_log(type, ...) bb_error_msg(__VA_ARGS__)

#define selinux_policy_root() "/sepolicy"

static int selinux_getenforcemode(int *rc)
{
	if (rc) {
		*rc = security_getenforce();
		return 0;
	}
	return -1;
}

static const char *selinux_file_contexts_path()
{
	return "/file_contexts";
}

#endif /* BB_ANDROID_SELINUX_H */
