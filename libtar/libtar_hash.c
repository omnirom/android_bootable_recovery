/* listhash/libtar_hash.c.  Generated from hash.c.in by configure. */

/*
**  Copyright 1998-2002 University of Illinois Board of Trustees
**  Copyright 1998-2002 Mark D. Roth
**  All rights reserved. 
**
**  libtar_hash.c - hash table routines
**
**  Mark D. Roth <roth@uiuc.edu>
**  Campus Information Technologies and Educational Services
**  University of Illinois at Urbana-Champaign
*/

#include <config.h>
#include <compat.h>

#include <libtar_listhash.h>

#include <stdio.h>
#include <errno.h>

#ifdef STDC_HEADERS
# include <stdlib.h>
#endif


/*
** libtar_hashptr_reset() - reset a hash pointer
*/
void
libtar_hashptr_reset(libtar_hashptr_t *hp)
{
	libtar_listptr_reset(&(hp->node));
	hp->bucket = -1;
}


/*
** libtar_hashptr_data() - retrieve the data being pointed to
*/
void *
libtar_hashptr_data(libtar_hashptr_t *hp)
{
	return libtar_listptr_data(&(hp->node));
}


/*
** libtar_str_hashfunc() - default hash function, optimized for
**				      7-bit strings
*/
unsigned int
libtar_str_hashfunc(char *key, unsigned int num_buckets)
{
#if 0
	register unsigned result = 0;
	register int i;

	if (key == NULL)
		return 0;

	for (i = 0; *key != '\0' && i < 32; i++)
		result = result * 33U + *key++;

	return (result % num_buckets);
#else
	if (key == NULL)
		return 0;

	return (key[0] % num_buckets);
#endif
}


/*
** libtar_hash_nents() - return number of elements from hash
*/
unsigned int
libtar_hash_nents(libtar_hash_t *h)
{
	return h->nents;
}


/*
** libtar_hash_new() - create a new hash
*/
libtar_hash_t *
libtar_hash_new(int num, libtar_hashfunc_t hashfunc)
{
	libtar_hash_t *hash;

	hash = (libtar_hash_t *)calloc(1, sizeof(libtar_hash_t));
	if (hash == NULL)
		return NULL;
	hash->numbuckets = num;
	if (hashfunc != NULL)
		hash->hashfunc = hashfunc;
	else
		hash->hashfunc = (libtar_hashfunc_t)libtar_str_hashfunc;

	hash->table = (libtar_list_t **)calloc(num, sizeof(libtar_list_t *));
	if (hash->table == NULL)
	{
		free(hash);
		return NULL;
	}

	return hash;
}


/*
** libtar_hash_next() - get next element in hash
** returns:
**	1			data found
**	0			end of list
*/
int
libtar_hash_next(libtar_hash_t *h,
			    libtar_hashptr_t *hp)
{
#ifdef DS_DEBUG
	printf("==> libtar_hash_next(h=0x%lx, hp={%d,0x%lx})\n",
	       h, hp->bucket, hp->node);
#endif

	if (hp->bucket >= 0 && hp->node != NULL &&
	    libtar_list_next(h->table[hp->bucket], &(hp->node)) != 0)
	{
#ifdef DS_DEBUG
		printf("    libtar_hash_next(): found additional "
		       "data in current bucket (%d), returing 1\n",
		       hp->bucket);
#endif
		return 1;
	}

#ifdef DS_DEBUG
	printf("    libtar_hash_next(): done with bucket %d\n",
	       hp->bucket);
#endif

	for (hp->bucket++; hp->bucket < h->numbuckets; hp->bucket++)
	{
#ifdef DS_DEBUG
		printf("    libtar_hash_next(): "
		       "checking bucket %d\n", hp->bucket);
#endif
		hp->node = NULL;
		if (h->table[hp->bucket] != NULL &&
		    libtar_list_next(h->table[hp->bucket],
		    				&(hp->node)) != 0)
		{
#ifdef DS_DEBUG
			printf("    libtar_hash_next(): "
			       "found data in bucket %d, returing 1\n",
			       hp->bucket);
#endif
			return 1;
		}
	}

	if (hp->bucket == h->numbuckets)
	{
#ifdef DS_DEBUG
		printf("    libtar_hash_next(): hash pointer "
		       "wrapped to 0\n");
#endif
		hp->bucket = -1;
		hp->node = NULL;
	}

#ifdef DS_DEBUG
	printf("<== libtar_hash_next(): no more data, "
	       "returning 0\n");
#endif
	return 0;
}


/*
** libtar_hash_del() - delete an entry from the hash
** returns:
**	0			success
**	-1 (and sets errno)	failure
*/
int
libtar_hash_del(libtar_hash_t *h,
			   libtar_hashptr_t *hp)
{
	if (hp->bucket < 0
	    || hp->bucket >= h->numbuckets
	    || h->table[hp->bucket] == NULL
	    || hp->node == NULL)
	{
		errno = EINVAL;
		return -1;
	}

	libtar_list_del(h->table[hp->bucket], &(hp->node));
	h->nents--;
	return 0;
}


/*
** libtar_hash_empty() - empty the hash
*/
void
libtar_hash_empty(libtar_hash_t *h, libtar_freefunc_t freefunc)
{
	int i;

	for (i = 0; i < h->numbuckets; i++)
		if (h->table[i] != NULL)
			libtar_list_empty(h->table[i], freefunc);

	h->nents = 0;
}


/*
** libtar_hash_free() - delete all of the nodes in the hash
*/
void
libtar_hash_free(libtar_hash_t *h, libtar_freefunc_t freefunc)
{
	int i;

	for (i = 0; i < h->numbuckets; i++)
		if (h->table[i] != NULL)
			libtar_list_free(h->table[i], freefunc);

	free(h->table);
	free(h);
}


/*
** libtar_hash_search() - iterative search for an element in a hash
** returns:
**	1			match found
**	0			no match
*/
int
libtar_hash_search(libtar_hash_t *h,
			      libtar_hashptr_t *hp, void *data,
			      libtar_matchfunc_t matchfunc)
{
	while (libtar_hash_next(h, hp) != 0)
		if ((*matchfunc)(data, libtar_listptr_data(&(hp->node))) != 0)
			return 1;

	return 0;
}


/*
** libtar_hash_getkey() - hash-based search for an element in a hash
** returns:
**	1			match found
**	0			no match
*/
int
libtar_hash_getkey(libtar_hash_t *h,
			      libtar_hashptr_t *hp, void *key,
			      libtar_matchfunc_t matchfunc)
{
#ifdef DS_DEBUG
	printf("==> libtar_hash_getkey(h=0x%lx, hp={%d,0x%lx}, "
	       "key=0x%lx, matchfunc=0x%lx)\n",
	       h, hp->bucket, hp->node, key, matchfunc);
#endif

	if (hp->bucket == -1)
	{
		hp->bucket = (*(h->hashfunc))(key, h->numbuckets);
#ifdef DS_DEBUG
		printf("    libtar_hash_getkey(): hp->bucket "
		       "set to %d\n", hp->bucket);
#endif
	}

	if (h->table[hp->bucket] == NULL)
	{
#ifdef DS_DEBUG
		printf("    libtar_hash_getkey(): no list "
		       "for bucket %d, returning 0\n", hp->bucket);
#endif
		hp->bucket = -1;
		return 0;
	}

#ifdef DS_DEBUG
	printf("<== libtar_hash_getkey(): "
	       "returning libtar_list_search()\n");
#endif
	return libtar_list_search(h->table[hp->bucket], &(hp->node),
					     key, matchfunc);
}


/*
** libtar_hash_add() - add an element to the hash
** returns:
**	0			success
**	-1 (and sets errno)	failure
*/
int
libtar_hash_add(libtar_hash_t *h, void *data)
{
	int bucket, i;

#ifdef DS_DEBUG
	printf("==> libtar_hash_add(h=0x%lx, data=0x%lx)\n",
	       h, data);
#endif

	bucket = (*(h->hashfunc))(data, h->numbuckets);
#ifdef DS_DEBUG
	printf("    libtar_hash_add(): inserting in bucket %d\n",
	       bucket);
#endif
	if (h->table[bucket] == NULL)
	{
#ifdef DS_DEBUG
		printf("    libtar_hash_add(): creating new list\n");
#endif
		h->table[bucket] = libtar_list_new(LIST_QUEUE, NULL);
	}

#ifdef DS_DEBUG
	printf("<== libtar_hash_add(): "
	       "returning libtar_list_add()\n");
#endif
	i = libtar_list_add(h->table[bucket], data);
	if (i == 0)
		h->nents++;
	return i;
}


