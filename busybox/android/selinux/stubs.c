#include <libbb.h>
#include <selinux/selinux.h>

/* create a new context with user name (may be unsafe) */
int get_default_context(const char* user,
	const char* fromcon UNUSED_PARAM,
	char ** newcon)
{
	char fmt[] = "u:r:%s:s0\0";
	int len = strlen(user) + strlen(fmt);

	*newcon = malloc(len);
	if (!(*newcon))
		return -1;
	snprintf(*newcon, len, fmt, user);
	return 0;
}

/* Compute a relabeling decision and set *newcon to refer to it.
   Caller must free via freecon.
   Stub not implemented in bionic, but declared in selinux.h */
int security_compute_relabel(const char *scon UNUSED_PARAM,
	const char *tcon,
	security_class_t tclass UNUSED_PARAM,
	char ** newcon)
{
	if (tcon)
		*newcon = strdup(tcon);
	if (!(*newcon))
		return -1;
	return 0;
}

/* Check a permission in the passwd class.
   Return 0 if granted or -1 otherwise. */
int selinux_check_passwd_access(access_vector_t requested UNUSED_PARAM)
{
	return 0;
}
