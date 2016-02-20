/* @(#)pmap_rmt.h	2.1 88/07/29 4.0 RPCSRC; from 1.2 88/02/08 SMI */
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

/*
 * Structures and XDR routines for parameters to and replies from
 * the portmapper remote-call-service.
 *
 * Copyright (C) 1986, Sun Microsystems, Inc.
 */

#ifndef _RPC_PMAP_RMT_H
#define _RPC_PMAP_RMT_H	1

#include <features.h>
#include <sys/types.h>
#include <rpc/types.h>
#include <rpc/xdr.h>

__BEGIN_DECLS

struct rmtcallargs {
	u_long prog, vers, proc, arglen;
	caddr_t args_ptr;
	xdrproc_t xdr_args;
};

extern bool_t xdr_rmtcall_args (XDR *__xdrs, struct rmtcallargs *__crp)
     __THROW;
libc_hidden_proto(xdr_rmtcall_args)


struct rmtcallres {
	u_long *port_ptr;
	u_long resultslen;
	caddr_t results_ptr;
	xdrproc_t xdr_results;
};

extern bool_t xdr_rmtcallres (XDR *__xdrs, struct rmtcallres *__crp) __THROW;
libc_hidden_proto(xdr_rmtcallres)

__END_DECLS

#endif /* rpc/pmap_rmt.h */
