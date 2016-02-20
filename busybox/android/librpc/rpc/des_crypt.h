/*
 * @(#)des_crypt.h	2.1 88/08/11 4.0 RPCSRC;	from 1.4 88/02/08 (C) 1986 SMI
 *
 * des_crypt.h, des library routine interface
 * Copyright (C) 1986, Sun Microsystems, Inc.
 */
/*
 * Sun RPC is a product of Sun Microsystems, Inc. and is provided for
 * unrestricted use provided that this legend is included on all tape
 * media and as a part of the software program in whole or part.  Users
 * may copy or modify Sun RPC without charge, but are not authorized
 * to license or distribute it to anyone else except as part of a product or
 * program developed by the user.
 *
 * SUN RPC IS PROVIDED AS IS WITH NO WARRANTIES OF ANY KIND INCLUDING THE
 * WARRANTIES OF DESIGN, MERCHANTIBILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE, OR ARISING FROM A COURSE OF DEALING, USAGE OR TRADE PRACTICE.
 *
 * Sun RPC is provided with no support and without any obligation on the
 * part of Sun Microsystems, Inc. to assist in its use, correction,
 * modification or enhancement.
 *
 * SUN MICROSYSTEMS, INC. SHALL HAVE NO LIABILITY WITH RESPECT TO THE
 * INFRINGEMENT OF COPYRIGHTS, TRADE SECRETS OR ANY PATENTS BY SUN RPC
 * OR ANY PART THEREOF.
 *
 * In no event will Sun Microsystems, Inc. be liable for any lost revenue
 * or profits or other special, indirect and consequential damages, even if
 * Sun has been advised of the possibility of such damages.
 *
 * Sun Microsystems, Inc.
 * 2550 Garcia Avenue
 * Mountain View, California  94043
 */

#ifndef __DES_CRYPT_H__
#define __DES_CRYPT_H__ 1

#include <features.h>

__BEGIN_DECLS

#define DES_MAXDATA 8192	/* max bytes encrypted in one call */
#define DES_DIRMASK (1 << 0)
#define DES_ENCRYPT (0*DES_DIRMASK)	/* Encrypt */
#define DES_DECRYPT (1*DES_DIRMASK)	/* Decrypt */


#define DES_DEVMASK (1 << 1)
#define	DES_HW (0*DES_DEVMASK)	/* Use hardware device */
#define DES_SW (1*DES_DEVMASK)	/* Use software device */


#define DESERR_NONE 0	/* succeeded */
#define DESERR_NOHWDEVICE 1	/* succeeded, but hw device not available */
#define DESERR_HWERROR 2	/* failed, hardware/driver error */
#define DESERR_BADPARAM 3	/* failed, bad parameter to call */

#define DES_FAILED(err) \
	((err) > DESERR_NOHWDEVICE)

/*
 * cbc_crypt()
 * ecb_crypt()
 *
 * Encrypt (or decrypt) len bytes of a buffer buf.
 * The length must be a multiple of eight.
 * The key should have odd parity in the low bit of each byte.
 * ivec is the input vector, and is updated to the new one (cbc only).
 * The mode is created by oring together the appropriate parameters.
 * DESERR_NOHWDEVICE is returned if DES_HW was specified but
 * there was no hardware to do it on (the data will still be
 * encrypted though, in software).
 */


/*
 * Cipher Block Chaining mode
 */
extern int cbc_crypt (char *__key, char *__buf, unsigned __len,
		      unsigned __mode, char *__ivec) __THROW;

/*
 * Electronic Code Book mode
 */
extern int ecb_crypt (char *__key, char *__buf, unsigned __len,
		      unsigned __mode) __THROW;

/*
 * Set des parity for a key.
 * DES parity is odd and in the low bit of each byte
 */
extern void des_setparity (char *__key) __THROW;

__END_DECLS

#endif
