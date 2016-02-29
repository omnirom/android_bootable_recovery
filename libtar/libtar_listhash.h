/* listhash/libtar_listhash.h.  Generated from listhash.h.in by configure. */

/*
**  Copyright 1998-2002 University of Illinois Board of Trustees
**  Copyright 1998-2002 Mark D. Roth
**  All rights reserved.
**
**  libtar_listhash.h - header file for listhash module
**
**  Mark D. Roth <roth@uiuc.edu>
**  Campus Information Technologies and Educational Services
**  University of Illinois at Urbana-Champaign
*/

#ifndef libtar_LISTHASH_H
#define libtar_LISTHASH_H

#ifdef __cplusplus
extern "C" {
#endif

/***** list.c **********************************************************/

/*
** Comparison function (used to determine order of elements in a list)
** returns less than, equal to, or greater than 0
** if data1 is less than, equal to, or greater than data2
*/
typedef int (*libtar_cmpfunc_t)(void *, void *);

/*
** Free function (for freeing allocated memory in each element)
*/
typedef void (*libtar_freefunc_t)(void *);

/*
** Plugin function for libtar_list_iterate()
*/
typedef int (*libtar_iterate_func_t)(void *, void *);

/*
** Matching function (used to find elements in a list)
** first argument is the data to search for
** second argument is the list element it's being compared to
** returns 0 if no match is found, non-zero otherwise
*/
typedef int (*libtar_matchfunc_t)(void *, void *);


struct libtar_node
{
	void *data;
	struct libtar_node *next;
	struct libtar_node *prev;
};
typedef struct libtar_node *libtar_listptr_t;

struct libtar_list
{
	libtar_listptr_t first;
	libtar_listptr_t last;
	libtar_cmpfunc_t cmpfunc;
	int flags;
	unsigned int nents;
};
typedef struct libtar_list libtar_list_t;


/* values for flags */
#define LIST_USERFUNC	0	/* use cmpfunc() to order */
#define LIST_STACK	1	/* new elements go in front */
#define LIST_QUEUE	2	/* new elements go at the end */


/* reset a list pointer */
void libtar_listptr_reset(libtar_listptr_t *);

/* retrieve the data being pointed to */
void *libtar_listptr_data(libtar_listptr_t *);

/* creates a new, empty list */
libtar_list_t *libtar_list_new(int, libtar_cmpfunc_t);

/* call a function for every element in a list */
int libtar_list_iterate(libtar_list_t *,
				   libtar_iterate_func_t, void *);

/* empty the list */
void libtar_list_empty(libtar_list_t *,
				  libtar_freefunc_t);

/* remove and free() the entire list */
void libtar_list_free(libtar_list_t *,
				 libtar_freefunc_t);

/* add elements */
int libtar_list_add(libtar_list_t *, void *);

/* removes an element from the list - returns -1 on error */
void libtar_list_del(libtar_list_t *,
				libtar_listptr_t *);

/* returns 1 when valid data is returned, or 0 at end of list */
int libtar_list_next(libtar_list_t *,
				libtar_listptr_t *);

/* returns 1 when valid data is returned, or 0 at end of list */
int libtar_list_prev(libtar_list_t *,
				libtar_listptr_t *);

/* return 1 if the data matches a list entry, 0 otherwise */
int libtar_list_search(libtar_list_t *,
				  libtar_listptr_t *, void *,
				  libtar_matchfunc_t);

/* return number of elements from list */
unsigned int libtar_list_nents(libtar_list_t *);

/* adds elements from a string delimited by delim */
int libtar_list_add_str(libtar_list_t *, char *, char *);

/* string matching function */
int libtar_str_match(char *, char *);


/***** hash.c **********************************************************/

/*
** Hashing function (determines which bucket the given key hashes into)
** first argument is the key to hash
** second argument is the total number of buckets
** returns the bucket number
*/
typedef unsigned int (*libtar_hashfunc_t)(void *, unsigned int);


struct libtar_hashptr
{
	int bucket;
	libtar_listptr_t node;
};
typedef struct libtar_hashptr libtar_hashptr_t;

struct libtar_hash
{
	int numbuckets;
	libtar_list_t **table;
	libtar_hashfunc_t hashfunc;
	unsigned int nents;
};
typedef struct libtar_hash libtar_hash_t;


/* reset a hash pointer */
void libtar_hashptr_reset(libtar_hashptr_t *);

/* retrieve the data being pointed to */
void *libtar_hashptr_data(libtar_hashptr_t *);

/* default hash function, optimized for 7-bit strings */
unsigned int libtar_str_hashfunc(char *, unsigned int);

/* return number of elements from hash */
unsigned int libtar_hash_nents(libtar_hash_t *);

/* create a new hash */
libtar_hash_t *libtar_hash_new(int, libtar_hashfunc_t);

/* empty the hash */
void libtar_hash_empty(libtar_hash_t *,
				  libtar_freefunc_t);

/* delete all the libtar_nodes of the hash and clean up */
void libtar_hash_free(libtar_hash_t *,
				 libtar_freefunc_t);

/* returns 1 when valid data is returned, or 0 at end of list */
int libtar_hash_next(libtar_hash_t *,
				libtar_hashptr_t *);

/* return 1 if the data matches a list entry, 0 otherwise */
int libtar_hash_search(libtar_hash_t *,
				  libtar_hashptr_t *, void *,
				  libtar_matchfunc_t);

/* return 1 if the key matches a list entry, 0 otherwise */
int libtar_hash_getkey(libtar_hash_t *,
				  libtar_hashptr_t *, void *,
				  libtar_matchfunc_t);

/* inserting data */
int libtar_hash_add(libtar_hash_t *, void *);

/* delete an entry */
int libtar_hash_del(libtar_hash_t *,
			       libtar_hashptr_t *);

#ifdef __cplusplus
}
#endif

#endif /* ! libtar_LISTHASH_H */

