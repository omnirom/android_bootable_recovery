/* @(#)pmap_rmt.c	2.2 88/08/01 4.0 RPCSRC */
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
static char sccsid[] = "@(#)pmap_rmt.c 1.21 87/08/27 Copyr 1984 Sun Micro";
#endif

/*
 * pmap_rmt.c
 * Client interface to pmap rpc service.
 * remote call and broadcast service
 *
 * Copyright (C) 1984, Sun Microsystems, Inc.
 */

#define __FORCE_GLIBC
#include <features.h>

#include <unistd.h>
#include <string.h>
#include <rpc/rpc.h>
#include <rpc/pmap_prot.h>
#include <rpc/pmap_clnt.h>
#include <rpc/pmap_rmt.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <stdio.h>
#include <errno.h>
#undef	 _POSIX_SOURCE		/* Ultrix <sys/param.h> needs --roland@gnu */
#include <sys/param.h>		/* Ultrix needs before net/if --roland@gnu */
#include <net/if.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#define MAX_BROADCAST_SIZE 1400

#if defined(ANDROID) && !defined(BIONIC_L)
static struct in_addr inet_makeaddr(in_addr_t net, in_addr_t host)
{
	struct in_addr a;

	if (net <= 0x000000ff)
		a.s_addr = (net << 24) | (host & 0x000000ff);
	else if (net <= 0x0000ffff) 
		a.s_addr = (net << 16) | (host & 0x0000ffff);
	else if (net <= 0x00ffffff) 
		a.s_addr = (net <<  8) | (host & 0x00ffffff);
	else
		a.s_addr = net | host;
	a.s_addr = htonl(a.s_addr);
	return a;
}
static in_addr_t inet_netof(struct in_addr in)
{
	in_addr_t i = ntohl(in.s_addr);

	if (i < 0x80000000)
		return i >> 24;
	if (i < 0xc0000000)
		return i >> 16;
	return i >> 8;
}

#endif


extern u_long _create_xid (void) attribute_hidden;

static const struct timeval timeout = {3, 0};

/*
 * pmapper remote-call-service interface.
 * This routine is used to call the pmapper remote call service
 * which will look up a service program in the port maps, and then
 * remotely call that routine with the given parameters.  This allows
 * programs to do a lookup and call in one step.
 */
enum clnt_stat
pmap_rmtcall (struct sockaddr_in *addr, u_long prog, u_long vers, u_long proc,
			  xdrproc_t xdrargs, caddr_t argsp, xdrproc_t xdrres, caddr_t resp,
			  struct timeval tout, u_long *port_ptr)
{
  int _socket = -1;
  CLIENT *client;
  struct rmtcallargs a;
  struct rmtcallres r;
  enum clnt_stat stat;

  addr->sin_port = htons (PMAPPORT);
  client = clntudp_create (addr, PMAPPROG, PMAPVERS, timeout, &_socket);
  if (client != (CLIENT *) NULL)
    {
      a.prog = prog;
      a.vers = vers;
      a.proc = proc;
      a.args_ptr = argsp;
      a.xdr_args = xdrargs;
      r.port_ptr = port_ptr;
      r.results_ptr = resp;
      r.xdr_results = xdrres;
      stat = CLNT_CALL (client, PMAPPROC_CALLIT, (xdrproc_t)xdr_rmtcall_args,
			(caddr_t)&a, (xdrproc_t)xdr_rmtcallres,
			(caddr_t)&r, tout);
      CLNT_DESTROY (client);
    }
  else
    {
      stat = RPC_FAILED;
    }
  /* (void)close(_socket); CLNT_DESTROY already closed it */
  addr->sin_port = 0;
  return stat;
}


/*
 * XDR remote call arguments
 * written for XDR_ENCODE direction only
 */
bool_t
xdr_rmtcall_args (XDR *xdrs, struct rmtcallargs *cap)
{
  u_int lenposition, argposition, position;

  if (xdr_u_long (xdrs, &(cap->prog)) &&
      xdr_u_long (xdrs, &(cap->vers)) &&
      xdr_u_long (xdrs, &(cap->proc)))
    {
      u_long dummy_arglen = 0;
      lenposition = XDR_GETPOS (xdrs);
      if (!xdr_u_long (xdrs, &dummy_arglen))
	return FALSE;
      argposition = XDR_GETPOS (xdrs);
      if (!(*(cap->xdr_args)) (xdrs, cap->args_ptr))
	return FALSE;
      position = XDR_GETPOS (xdrs);
      cap->arglen = (u_long) position - (u_long) argposition;
      XDR_SETPOS (xdrs, lenposition);
      if (!xdr_u_long (xdrs, &(cap->arglen)))
	return FALSE;
      XDR_SETPOS (xdrs, position);
      return TRUE;
    }
  return FALSE;
}
libc_hidden_def(xdr_rmtcall_args)

/*
 * XDR remote call results
 * written for XDR_DECODE direction only
 */
bool_t
xdr_rmtcallres (XDR *xdrs, struct rmtcallres *crp)
{
  caddr_t port_ptr;

  port_ptr = (caddr_t) crp->port_ptr;
  if (xdr_reference (xdrs, &port_ptr, sizeof (u_long), (xdrproc_t) xdr_u_long)
      && xdr_u_long (xdrs, &crp->resultslen))
    {
      crp->port_ptr = (u_long *) port_ptr;
      return (*(crp->xdr_results)) (xdrs, crp->results_ptr);
    }
  return FALSE;
}
libc_hidden_def(xdr_rmtcallres)


/*
 * The following is kludged-up support for simple rpc broadcasts.
 * Someday a large, complicated system will replace these trivial
 * routines which only support udp/ip .
 */

static int
internal_function
getbroadcastnets (struct in_addr *addrs, int sock, char *buf)
  /* int sock:  any valid socket will do */
  /* char *buf:	why allocate more when we can use existing... */
{
  struct ifconf ifc;
  struct ifreq ifreq, *ifr;
  struct sockaddr_in *sin;
  int n, i;

  ifc.ifc_len = UDPMSGSIZE;
  ifc.ifc_buf = buf;
  if (ioctl (sock, SIOCGIFCONF, (char *) &ifc) < 0)
    {
      perror (_("broadcast: ioctl (get interface configuration)"));
      return (0);
    }
  ifr = ifc.ifc_req;
  for (i = 0, n = ifc.ifc_len / sizeof (struct ifreq); n > 0; n--, ifr++)
    {
      ifreq = *ifr;
      if (ioctl (sock, SIOCGIFFLAGS, (char *) &ifreq) < 0)
	{
	  perror (_("broadcast: ioctl (get interface flags)"));
	  continue;
	}
      if ((ifreq.ifr_flags & IFF_BROADCAST) &&
	  (ifreq.ifr_flags & IFF_UP) &&
	  ifr->ifr_addr.sa_family == AF_INET)
	{
	  sin = (struct sockaddr_in *) &ifr->ifr_addr;
#ifdef SIOCGIFBRDADDR		/* 4.3BSD */
	  if (ioctl (sock, SIOCGIFBRDADDR, (char *) &ifreq) < 0)
	    {
	      addrs[i++] = inet_makeaddr (inet_netof
	      /* Changed to pass struct instead of s_addr member
	         by roland@gnu.  */
					  (sin->sin_addr), INADDR_ANY);
	    }
	  else
	    {
	      addrs[i++] = ((struct sockaddr_in *)
			    &ifreq.ifr_addr)->sin_addr;
	    }
#else /* 4.2 BSD */
	  addrs[i++] = inet_makeaddr (inet_netof
				      (sin->sin_addr.s_addr), INADDR_ANY);
#endif
	}
    }
  return i;
}


enum clnt_stat
clnt_broadcast (
     u_long prog,		/* program number */
     u_long vers,		/* version number */
     u_long proc,		/* procedure number */
     xdrproc_t xargs,		/* xdr routine for args */
     caddr_t argsp,		/* pointer to args */
     xdrproc_t xresults,	/* xdr routine for results */
     caddr_t resultsp,		/* pointer to results */
     resultproc_t eachresult	/* call with each result obtained */)
{
  enum clnt_stat stat = RPC_FAILED;
  AUTH *unix_auth = authunix_create_default ();
  XDR xdr_stream;
  XDR *xdrs = &xdr_stream;
  struct timeval t;
  int outlen, inlen, nets;
  socklen_t fromlen;
  int sock;
  int on = 1;
  struct pollfd fd;
  int milliseconds;
  int i;
  bool_t done = FALSE;
  u_long xid;
  u_long port;
  struct in_addr addrs[20];
  struct sockaddr_in baddr, raddr;	/* broadcast and response addresses */
  struct rmtcallargs a;
  struct rmtcallres r;
  struct rpc_msg msg;
  char outbuf[MAX_BROADCAST_SIZE], inbuf[UDPMSGSIZE];

  /*
   * initialization: create a socket, a broadcast address, and
   * preserialize the arguments into a send buffer.
   */
  if ((sock = socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
    {
      perror (_("Cannot create socket for broadcast rpc"));
      stat = RPC_CANTSEND;
      goto done_broad;
    }
#ifdef SO_BROADCAST
  if (setsockopt (sock, SOL_SOCKET, SO_BROADCAST, &on, sizeof (on)) < 0)
    {
      perror (_("Cannot set socket option SO_BROADCAST"));
      stat = RPC_CANTSEND;
      goto done_broad;
    }
#endif /* def SO_BROADCAST */
  fd.fd = sock;
  fd.events = POLLIN;
  nets = getbroadcastnets (addrs, sock, inbuf);
  memset ((char *) &baddr, 0, sizeof (baddr));
  baddr.sin_family = AF_INET;
  baddr.sin_port = htons (PMAPPORT);
  baddr.sin_addr.s_addr = htonl (INADDR_ANY);
/*      baddr.sin_addr.S_un.S_addr = htonl(INADDR_ANY); */
  msg.rm_xid = xid = _create_xid ();
  t.tv_usec = 0;
  msg.rm_direction = CALL;
  msg.rm_call.cb_rpcvers = RPC_MSG_VERSION;
  msg.rm_call.cb_prog = PMAPPROG;
  msg.rm_call.cb_vers = PMAPVERS;
  msg.rm_call.cb_proc = PMAPPROC_CALLIT;
  msg.rm_call.cb_cred = unix_auth->ah_cred;
  msg.rm_call.cb_verf = unix_auth->ah_verf;
  a.prog = prog;
  a.vers = vers;
  a.proc = proc;
  a.xdr_args = xargs;
  a.args_ptr = argsp;
  r.port_ptr = &port;
  r.xdr_results = xresults;
  r.results_ptr = resultsp;
  xdrmem_create (xdrs, outbuf, MAX_BROADCAST_SIZE, XDR_ENCODE);
  if ((!xdr_callmsg (xdrs, &msg)) || (!xdr_rmtcall_args (xdrs, &a)))
    {
      stat = RPC_CANTENCODEARGS;
      goto done_broad;
    }
  outlen = (int) xdr_getpos (xdrs);
  xdr_destroy (xdrs);
  /*
   * Basic loop: broadcast a packet and wait a while for response(s).
   * The response timeout grows larger per iteration.
   */
  for (t.tv_sec = 4; t.tv_sec <= 14; t.tv_sec += 2)
    {
      for (i = 0; i < nets; i++)
	{
	  baddr.sin_addr = addrs[i];
	  if (sendto (sock, outbuf, outlen, 0,
		      (struct sockaddr *) &baddr,
		      sizeof (struct sockaddr)) != outlen)
	    {
	      perror (_("Cannot send broadcast packet"));
	      stat = RPC_CANTSEND;
	      goto done_broad;
	    }
	}
      if (eachresult == NULL)
	{
	  stat = RPC_SUCCESS;
	  goto done_broad;
	}
    recv_again:
      msg.acpted_rply.ar_verf = _null_auth;
      msg.acpted_rply.ar_results.where = (caddr_t) & r;
      msg.acpted_rply.ar_results.proc = (xdrproc_t) xdr_rmtcallres;
      milliseconds = t.tv_sec * 1000 + t.tv_usec / 1000;
      switch (poll(&fd, 1, milliseconds))
	{

	case 0:		/* timed out */
	  stat = RPC_TIMEDOUT;
	  continue;

	case -1:		/* some kind of error */
	  if (errno == EINTR)
	    goto recv_again;
	  perror (_("Broadcast poll problem"));
	  stat = RPC_CANTRECV;
	  goto done_broad;

	}			/* end of poll results switch */
    try_again:
      fromlen = sizeof (struct sockaddr);
      inlen = recvfrom (sock, inbuf, UDPMSGSIZE, 0,
			(struct sockaddr *) &raddr, &fromlen);
      if (inlen < 0)
	{
	  if (errno == EINTR)
	    goto try_again;
	  perror (_("Cannot receive reply to broadcast"));
	  stat = RPC_CANTRECV;
	  goto done_broad;
	}
      if ((size_t) inlen < sizeof (u_long))
	goto recv_again;
      /*
       * see if reply transaction id matches sent id.
       * If so, decode the results.
       */
      xdrmem_create (xdrs, inbuf, (u_int) inlen, XDR_DECODE);
      if (xdr_replymsg (xdrs, &msg))
	{
	  if (((u_int32_t) msg.rm_xid == (u_int32_t) xid) &&
	      (msg.rm_reply.rp_stat == MSG_ACCEPTED) &&
	      (msg.acpted_rply.ar_stat == SUCCESS))
	    {
	      raddr.sin_port = htons ((u_short) port);
	      done = (*eachresult) (resultsp, &raddr);
	    }
	  /* otherwise, we just ignore the errors ... */
	}
      else
	{
#ifdef notdef
	  /* some kind of deserialization problem ... */
	  if ((u_int32_t) msg.rm_xid == (u_int32_t) xid)
	    fprintf (stderr, "Broadcast deserialization problem");
	  /* otherwise, just random garbage */
#endif
	}
      xdrs->x_op = XDR_FREE;
      msg.acpted_rply.ar_results.proc = (xdrproc_t)xdr_void;
      (void) xdr_replymsg (xdrs, &msg);
      (void) (*xresults) (xdrs, resultsp);
      xdr_destroy (xdrs);
      if (done)
	{
	  stat = RPC_SUCCESS;
	  goto done_broad;
	}
      else
	{
	  goto recv_again;
	}
    }
done_broad:
  (void) close (sock);
  AUTH_DESTROY (unix_auth);
  return stat;
}
