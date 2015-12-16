/*
 * selinux_internal.h and label_internal.h definitions (libselinux)
 *
 */
#ifndef _SELINUX_BB_INTERNAL_H
#define _SELINUX_BB_INTERNAL_H	1

#include <selinux/selinux.h>
#include <selinux/label.h>
#include <pthread.h>

#ifdef SHARED
# define hidden __attribute__ ((visibility ("hidden")))
# define hidden_proto(fct) __hidden_proto (fct, fct##_internal)
# define __hidden_proto(fct, internal)	\
     extern __typeof (fct) internal;	\
     extern __typeof (fct) fct __asm (#internal) hidden;
# if defined(__alpha__) || defined(__mips__)
#  define hidden_def(fct) \
     asm (".globl " #fct "\n" #fct " = " #fct "_internal");
# else
#  define hidden_def(fct) \
     asm (".globl " #fct "\n.set " #fct ", " #fct "_internal");
#endif
#else
# define hidden
# define hidden_proto(fct)
# define hidden_def(fct)
#endif

hidden_proto(selinux_mkload_policy)
    hidden_proto(fini_selinuxmnt)
    hidden_proto(set_selinuxmnt)
    hidden_proto(selinuxfs_exists)
    hidden_proto(security_disable)
    hidden_proto(security_policyvers)
    hidden_proto(security_load_policy)
    hidden_proto(security_get_boolean_active)
    hidden_proto(security_get_boolean_names)
    hidden_proto(security_set_boolean)
    hidden_proto(security_commit_booleans)
    hidden_proto(security_check_context)
    hidden_proto(security_check_context_raw)
    hidden_proto(security_canonicalize_context)
    hidden_proto(security_canonicalize_context_raw)
    hidden_proto(security_compute_av)
    hidden_proto(security_compute_av_raw)
    hidden_proto(security_compute_av_flags)
    hidden_proto(security_compute_av_flags_raw)
    hidden_proto(security_compute_user)
    hidden_proto(security_compute_user_raw)
    hidden_proto(security_compute_create)
    hidden_proto(security_compute_create_raw)
    hidden_proto(security_compute_create_name)
    hidden_proto(security_compute_create_name_raw)
    hidden_proto(security_compute_member_raw)
    hidden_proto(security_compute_relabel_raw)
    hidden_proto(is_selinux_enabled)
    hidden_proto(is_selinux_mls_enabled)
    hidden_proto(freecon)
    hidden_proto(freeconary)
    hidden_proto(getprevcon)
    hidden_proto(getprevcon_raw)
    hidden_proto(getcon)
    hidden_proto(getcon_raw)
    hidden_proto(setcon_raw)
    hidden_proto(getpeercon_raw)
    hidden_proto(getpidcon_raw)
    hidden_proto(getexeccon_raw)
    hidden_proto(getfilecon)
    hidden_proto(getfilecon_raw)
    hidden_proto(lgetfilecon_raw)
    hidden_proto(fgetfilecon_raw)
    hidden_proto(setfilecon_raw)
    hidden_proto(lsetfilecon_raw)
    hidden_proto(fsetfilecon_raw)
    hidden_proto(setexeccon)
    hidden_proto(setexeccon_raw)
    hidden_proto(getfscreatecon_raw)
    hidden_proto(getkeycreatecon_raw)
    hidden_proto(getsockcreatecon_raw)
    hidden_proto(setfscreatecon_raw)
    hidden_proto(setkeycreatecon_raw)
    hidden_proto(setsockcreatecon_raw)
    hidden_proto(security_getenforce)
    hidden_proto(security_setenforce)
    hidden_proto(security_deny_unknown)
    hidden_proto(selinux_boolean_sub)
    hidden_proto(selinux_binary_policy_path)
    hidden_proto(selinux_booleans_subs_path)
    hidden_proto(selinux_default_context_path)
    hidden_proto(selinux_securetty_types_path)
    hidden_proto(selinux_failsafe_context_path)
    hidden_proto(selinux_removable_context_path)
    hidden_proto(selinux_virtual_domain_context_path)
    hidden_proto(selinux_virtual_image_context_path)
    hidden_proto(selinux_lxc_contexts_path)
    hidden_proto(selinux_file_context_path)
    hidden_proto(selinux_file_context_homedir_path)
    hidden_proto(selinux_file_context_local_path)
    hidden_proto(selinux_file_context_subs_dist_path)
    hidden_proto(selinux_file_context_subs_path)
    hidden_proto(selinux_netfilter_context_path)
    hidden_proto(selinux_homedir_context_path)
    hidden_proto(selinux_user_contexts_path)
    hidden_proto(selinux_booleans_path)
    hidden_proto(selinux_customizable_types_path)
    hidden_proto(selinux_media_context_path)
    hidden_proto(selinux_x_context_path)
    hidden_proto(selinux_sepgsql_context_path)
    hidden_proto(selinux_path)
    hidden_proto(selinux_check_passwd_access)
    hidden_proto(selinux_check_securetty_context)
    hidden_proto(matchpathcon_init_prefix)
    hidden_proto(selinux_users_path)
    hidden_proto(selinux_usersconf_path);
hidden_proto(selinux_translations_path);
hidden_proto(selinux_colors_path);
hidden_proto(selinux_getenforcemode);
hidden_proto(selinux_getpolicytype);
hidden_proto(selinux_raw_to_trans_context);
hidden_proto(selinux_trans_to_raw_context);
    hidden_proto(selinux_raw_context_to_color);
hidden_proto(security_get_initial_context);
hidden_proto(security_get_initial_context_raw);
hidden_proto(selinux_reset_config);

extern int load_setlocaldefs hidden;
extern int require_seusers hidden;
extern int selinux_page_size hidden;

/* Make pthread_once optional */
#pragma weak pthread_once
#pragma weak pthread_key_create
#pragma weak pthread_key_delete
#pragma weak pthread_setspecific

/* Call handler iff the first call.  */
#define __selinux_once(ONCE_CONTROL, INIT_FUNCTION)	\
	do {						\
		if (pthread_once != NULL)		\
			pthread_once (&(ONCE_CONTROL), (INIT_FUNCTION));  \
		else if ((ONCE_CONTROL) == PTHREAD_ONCE_INIT) {		  \
			INIT_FUNCTION ();		\
			(ONCE_CONTROL) = 2;		\
		}					\
	} while (0)

/* Pthread key macros */
#define __selinux_key_create(KEY, DESTRUCTOR)			\
	(pthread_key_create != NULL ? pthread_key_create(KEY, DESTRUCTOR) : -1)

#define __selinux_key_delete(KEY)				\
	do {							\
		if (pthread_key_delete != NULL)			\
			pthread_key_delete(KEY);		\
	} while (0)

#define __selinux_setspecific(KEY, VALUE)			\
	do {							\
		if (pthread_setspecific != NULL)		\
			pthread_setspecific(KEY, VALUE);	\
	} while (0)


/*
 * Installed backends
 */
int selabel_file_init(struct selabel_handle *rec, struct selinux_opt *opts,
		      unsigned nopts) hidden;
int selabel_media_init(struct selabel_handle *rec, struct selinux_opt *opts,
		      unsigned nopts) hidden;
int selabel_x_init(struct selabel_handle *rec, struct selinux_opt *opts,
		   unsigned nopts) hidden;
int selabel_db_init(struct selabel_handle *rec,
		    struct selinux_opt *opts, unsigned nopts) hidden;
int selabel_property_init(struct selabel_handle *rec,
			  struct selinux_opt *opts, unsigned nopts) hidden;

/*
 * Labeling internal structures
 */
struct selabel_sub {
	char *src;
	int slen;
	char *dst;
	struct selabel_sub *next;
};

extern struct selabel_sub *selabel_subs_init(const char *path,
					     struct selabel_sub *list);

struct selabel_lookup_rec {
	security_context_t ctx_raw;
	security_context_t ctx_trans;
	int validated;
};

struct selabel_handle {
	/* arguments that were passed to selabel_open */
	unsigned int backend;
	int validating;

	/* labeling operations */
	struct selabel_lookup_rec *(*func_lookup) (struct selabel_handle *h,
						   const char *key, int type);
	void (*func_close) (struct selabel_handle *h);
	void (*func_stats) (struct selabel_handle *h);

	/* supports backend-specific state information */
	void *data;
#if 0
	/*
	 * The main spec file used. Note for file contexts the local and/or
	 * homedirs could also have been used to resolve a context.
	 */
	char *spec_file;
#endif
	/* substitution support */
	struct selabel_sub *subs;
};

/*
 * Validation function
 */
extern int
selabel_validate(struct selabel_handle *rec,
		 struct selabel_lookup_rec *contexts) hidden;

/*
 * Compatibility support
 */
extern int myprintf_compat;
extern void __attribute__ ((format(printf, 1, 2)))
(*myprintf) (const char *fmt,...);

#define COMPAT_LOG(type, fmt...) if (myprintf_compat)	  \
		myprintf(fmt);				  \
	else						  \
		selinux_log(type, fmt);

extern int
compat_validate(struct selabel_handle *rec,
		struct selabel_lookup_rec *contexts,
		const char *path, unsigned lineno) hidden;


#endif // _SELINUX_BB_INTERNAL_H
