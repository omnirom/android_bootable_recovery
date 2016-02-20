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
 * Copyright (c) 1987 by Sun Microsystems, Inc.
 */

#define __FORCE_GLIBC
#include <features.h>

#include <errno.h>
#include <unistd.h>
#include <string.h>
#ifdef ANDROID
#include <rpc/types.h>
extern long __set_errno(int n);
#else
#include <sys/types.h>
#endif
#include <sys/socket.h>
#include <netinet/in.h>


/*
 * Bind a socket to a privileged IP port
 */
int
bindresvport (int sd, struct sockaddr_in *sin)
{
  int res;
  static short port;
  struct sockaddr_in myaddr;
  int i;

#define STARTPORT 600
#define ENDPORT (IPPORT_RESERVED - 1)
#define NPORTS	(ENDPORT - STARTPORT + 1)

  if (sin == (struct sockaddr_in *) 0)
    {
      sin = &myaddr;
      memset (sin, 0, sizeof (*sin));
      sin->sin_family = AF_INET;
    }
  else if (sin->sin_family != AF_INET)
    {
      __set_errno (EPFNOSUPPORT);
      return -1;
    }

  if (port == 0)
    {
      port = (getpid () % NPORTS) + STARTPORT;
    }
  res = -1;
  __set_errno (EADDRINUSE);

  for (i = 0; i < NPORTS && res < 0 && errno == EADDRINUSE; ++i)
    {
      sin->sin_port = htons (port);
      if (++port > ENDPORT)
	{
	  port = STARTPORT;
	}
      res = bind(sd, (struct sockaddr *)sin, sizeof(struct sockaddr_in));
    }

  return res;
}
libc_hidden_def(bindresvport)
