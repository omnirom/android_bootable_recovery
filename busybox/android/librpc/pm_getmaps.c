/* @(#)pmap_getmaps.c	2.2 88/08/01 4.0 RPCSRC */
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
#if 0
static char sccsid[] = "@(#)pmap_getmaps.c 1.10 87/08/11 Copyr 1984 Sun Micro";
#endif

/*
 * pmap_getmap.c
 * Client interface to pmap rpc service.
 * contains pmap_getmaps, which is only tcp service involved
 *
 * Copyright (C) 1984, Sun Microsystems, Inc.
 */

#include <rpc/rpc.h>
#include <rpc/pmap_prot.h>
#include <rpc/pmap_clnt.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>


/*
 * Get a copy of the current port maps.
 * Calls the pmap service remotely to do get the maps.
 */
struct pmaplist *
pmap_getmaps (struct sockaddr_in *address)
{
  struct pmaplist *head = (struct pmaplist *) NULL;
  int _socket = -1;
  struct timeval minutetimeout;
  CLIENT *client;

  minutetimeout.tv_sec = 60;
  minutetimeout.tv_usec = 0;
  address->sin_port = htons (PMAPPORT);

  /* Don't need a reserved port to get ports from the portmapper.  */
  client = clnttcp_create (address, PMAPPROG,
			   PMAPVERS, &_socket, 50, 500);
  if (client != (CLIENT *) NULL)
    {
      if (CLNT_CALL (client, PMAPPROC_DUMP, (xdrproc_t)xdr_void, NULL,
		     (xdrproc_t)xdr_pmaplist, (caddr_t)&head,
		     minutetimeout) != RPC_SUCCESS)
	{
	  clnt_perror (client, _("pmap_getmaps rpc problem"));
	}
      CLNT_DESTROY (client);
    }
  /* (void)__close(_socket); CLNT_DESTROY already closed it */
  address->sin_port = 0;
  return head;
}
