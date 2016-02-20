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
 * svc.h, Server-side remote procedure call interface.
 *
 * Copyright (C) 1984, Sun Microsystems, Inc.
 */

#ifndef _RPC_SVC_H
#define _RPC_SVC_H 1

#include <features.h>
#include <rpc/rpc_msg.h>

__BEGIN_DECLS

/*
 * This interface must manage two items concerning remote procedure calling:
 *
 * 1) An arbitrary number of transport connections upon which rpc requests
 * are received.  The two most notable transports are TCP and UDP;  they are
 * created and registered by routines in svc_tcp.c and svc_udp.c, respectively;
 * they in turn call xprt_register and xprt_unregister.
 *
 * 2) An arbitrary number of locally registered services.  Services are
 * described by the following four data: program number, version number,
 * "service dispatch" function, a transport handle, and a boolean that
 * indicates whether or not the exported program should be registered with a
 * local binder service;  if true the program's number and version and the
 * port number from the transport handle are registered with the binder.
 * These data are registered with the rpc svc system via svc_register.
 *
 * A service's dispatch function is called whenever an rpc request comes in
 * on a transport.  The request's program and version numbers must match
 * those of the registered service.  The dispatch function is passed two
 * parameters, struct svc_req * and SVCXPRT *, defined below.
 */

enum xprt_stat {
	XPRT_DIED,
	XPRT_MOREREQS,
	XPRT_IDLE
};

/*
 * Server side transport handle
 */
typedef struct SVCXPRT SVCXPRT;
struct SVCXPRT {
  int xp_sock;
  u_short xp_port;		/* associated port number */
  const struct xp_ops {
    bool_t	(*xp_recv) (SVCXPRT *__xprt, struct rpc_msg *__msg);
				/* receive incoming requests */
    enum xprt_stat (*xp_stat) (SVCXPRT *__xprt);
				/* get transport status */
    bool_t	(*xp_getargs) (SVCXPRT *__xprt, xdrproc_t __xdr_args,
			       caddr_t args_ptr); /* get arguments */
    bool_t	(*xp_reply) (SVCXPRT *__xprt, struct rpc_msg *__msg);
				/* send reply */
    bool_t	(*xp_freeargs) (SVCXPRT *__xprt, xdrproc_t __xdr_args,
				caddr_t args_ptr);
				/* free mem allocated for args */
    void	(*xp_destroy) (SVCXPRT *__xprt);
				/* destroy this struct */
  } *xp_ops;
  int		xp_addrlen;	 /* length of remote address */
  struct sockaddr_in xp_raddr;	 /* remote address */
  struct opaque_auth xp_verf;	 /* raw response verifier */
  caddr_t		xp_p1;		 /* private */
  caddr_t		xp_p2;		 /* private */
  char		xp_pad [256];	/* padding, internal use */
};

/*
 *  Approved way of getting address of caller
 */
#define svc_getcaller(x) (&(x)->xp_raddr)

/*
 * Operations defined on an SVCXPRT handle
 *
 * SVCXPRT		*xprt;
 * struct rpc_msg	*msg;
 * xdrproc_t		 xargs;
 * caddr_t		 argsp;
 */
#define SVC_RECV(xprt, msg)				\
	(*(xprt)->xp_ops->xp_recv)((xprt), (msg))
#define svc_recv(xprt, msg)				\
	(*(xprt)->xp_ops->xp_recv)((xprt), (msg))

#define SVC_STAT(xprt)					\
	(*(xprt)->xp_ops->xp_stat)(xprt)
#define svc_stat(xprt)					\
	(*(xprt)->xp_ops->xp_stat)(xprt)

#define SVC_GETARGS(xprt, xargs, argsp)			\
	(*(xprt)->xp_ops->xp_getargs)((xprt), (xargs), (argsp))
#define svc_getargs(xprt, xargs, argsp)			\
	(*(xprt)->xp_ops->xp_getargs)((xprt), (xargs), (argsp))

#define SVC_REPLY(xprt, msg)				\
	(*(xprt)->xp_ops->xp_reply) ((xprt), (msg))
#define svc_reply(xprt, msg)				\
	(*(xprt)->xp_ops->xp_reply) ((xprt), (msg))

#define SVC_FREEARGS(xprt, xargs, argsp)		\
	(*(xprt)->xp_ops->xp_freeargs)((xprt), (xargs), (argsp))
#define svc_freeargs(xprt, xargs, argsp)		\
	(*(xprt)->xp_ops->xp_freeargs)((xprt), (xargs), (argsp))

#define SVC_DESTROY(xprt)				\
	(*(xprt)->xp_ops->xp_destroy)(xprt)
#define svc_destroy(xprt)				\
	(*(xprt)->xp_ops->xp_destroy)(xprt)


/*
 * Service request
 */
struct svc_req {
  rpcprog_t rq_prog;            /* service program number */
  rpcvers_t rq_vers;            /* service protocol version */
  rpcproc_t rq_proc;            /* the desired procedure */
  struct opaque_auth rq_cred;   /* raw creds from the wire */
  caddr_t rq_clntcred;          /* read only cooked cred */
  SVCXPRT *rq_xprt;             /* associated transport */
};

#ifndef __DISPATCH_FN_T
#define __DISPATCH_FN_T
typedef void (*__dispatch_fn_t) (struct svc_req*, SVCXPRT*);
#endif

/*
 * Service registration
 *
 * svc_register(xprt, prog, vers, dispatch, protocol)
 *	SVCXPRT *xprt;
 *	rpcprog_t prog;
 *	rpcvers_t vers;
 *	void (*dispatch)(struct svc_req*, SVCXPRT*);
 *	rpcprot_t protocol;  like TCP or UDP, zero means do not register
 */
extern bool_t svc_register (SVCXPRT *__xprt, rpcprog_t __prog,
			    rpcvers_t __vers, __dispatch_fn_t __dispatch,
			    rpcprot_t __protocol) __THROW;
libc_hidden_proto(svc_register)

/*
 * Service un-registration
 *
 * svc_unregister(prog, vers)
 *	rpcprog_t prog;
 *	rpcvers_t vers;
 */
extern void svc_unregister (rpcprog_t __prog, rpcvers_t __vers) __THROW;
libc_hidden_proto(svc_unregister)

/*
 * Transport registration.
 *
 * xprt_register(xprt)
 *	SVCXPRT *xprt;
 */
extern void xprt_register (SVCXPRT *__xprt) __THROW;
libc_hidden_proto(xprt_register)

/*
 * Transport un-register
 *
 * xprt_unregister(xprt)
 *	SVCXPRT *xprt;
 */
extern void xprt_unregister (SVCXPRT *__xprt) __THROW;
libc_hidden_proto(xprt_unregister)

/*
 * When the service routine is called, it must first check to see if it
 * knows about the procedure;  if not, it should call svcerr_noproc
 * and return.  If so, it should deserialize its arguments via
 * SVC_GETARGS (defined above).  If the deserialization does not work,
 * svcerr_decode should be called followed by a return.  Successful
 * decoding of the arguments should be followed the execution of the
 * procedure's code and a call to svc_sendreply.
 *
 * Also, if the service refuses to execute the procedure due to too-
 * weak authentication parameters, svcerr_weakauth should be called.
 * Note: do not confuse access-control failure with weak authentication!
 *
 * NB: In pure implementations of rpc, the caller always waits for a reply
 * msg.  This message is sent when svc_sendreply is called.
 * Therefore pure service implementations should always call
 * svc_sendreply even if the function logically returns void;  use
 * xdr.h - xdr_void for the xdr routine.  HOWEVER, tcp based rpc allows
 * for the abuse of pure rpc via batched calling or pipelining.  In the
 * case of a batched call, svc_sendreply should NOT be called since
 * this would send a return message, which is what batching tries to avoid.
 * It is the service/protocol writer's responsibility to know which calls are
 * batched and which are not.  Warning: responding to batch calls may
 * deadlock the caller and server processes!
 */

extern bool_t	svc_sendreply (SVCXPRT *xprt, xdrproc_t __xdr_results,
			       caddr_t __xdr_location) __THROW;
libc_hidden_proto(svc_sendreply)

extern void	svcerr_decode (SVCXPRT *__xprt) __THROW;
libc_hidden_proto(svcerr_decode)

extern void	svcerr_weakauth (SVCXPRT *__xprt) __THROW;

extern void	svcerr_noproc (SVCXPRT *__xprt) __THROW;

extern void	svcerr_progvers (SVCXPRT *__xprt, rpcvers_t __low_vers,
				 rpcvers_t __high_vers) __THROW;
libc_hidden_proto(svcerr_progvers)

extern void	svcerr_auth (SVCXPRT *__xprt, enum auth_stat __why) __THROW;
libc_hidden_proto(svcerr_auth)

extern void	svcerr_noprog (SVCXPRT *__xprt) __THROW;
libc_hidden_proto(svcerr_noprog)

extern void	svcerr_systemerr (SVCXPRT *__xprt) __THROW;

/*
 * Lowest level dispatching -OR- who owns this process anyway.
 * Somebody has to wait for incoming requests and then call the correct
 * service routine.  The routine svc_run does infinite waiting; i.e.,
 * svc_run never returns.
 * Since another (coexistent) package may wish to selectively wait for
 * incoming calls or other events outside of the rpc architecture, the
 * routine svc_getreq is provided.  It must be passed readfds, the
 * "in-place" results of a select system call (see select, section 2).
 */

/*
 * Global keeper of rpc service descriptors in use
 * dynamic; must be inspected before each call to select
 */

extern struct pollfd *svc_pollfd;
extern int svc_max_pollfd;
extern fd_set svc_fdset;
#define svc_fds svc_fdset.fds_bits[0]	/* compatibility */

/*
 * a small program implemented by the svc_rpc implementation itself;
 * also see clnt.h for protocol numbers.
 */
extern void svc_getreq (int __rdfds) __THROW;
libc_hidden_proto(svc_getreq)
extern void svc_getreq_common (const int __fd) __THROW;
libc_hidden_proto(svc_getreq_common)
extern void svc_getreqset (fd_set *__readfds) __THROW;
libc_hidden_proto(svc_getreqset)
extern void svc_getreq_poll (struct pollfd *, const int) __THROW;
libc_hidden_proto(svc_getreq_poll)
extern void svc_exit (void) __THROW;
extern void svc_run (void) __THROW;

/*
 * Socket to use on svcxxx_create call to get default socket
 */
#define	RPC_ANYSOCK	-1

/*
 * These are the existing service side transport implementations
 */

/*
 * Memory based rpc for testing and timing.
 */
extern SVCXPRT *svcraw_create (void) __THROW;

/*
 * Udp based rpc.
 */
extern SVCXPRT *svcudp_create (int __sock) __THROW;
libc_hidden_proto(svcudp_create)
extern SVCXPRT *svcudp_bufcreate (int __sock, u_int __sendsz, u_int __recvsz)
     __THROW;
libc_hidden_proto(svcudp_bufcreate)

/*
 * Tcp based rpc.
 */
extern SVCXPRT *svctcp_create (int __sock, u_int __sendsize, u_int __recvsize)
     __THROW;


/*
 * Unix based rpc.
 */
extern SVCXPRT *svcunix_create (int __sock, u_int __sendsize, u_int __recvsize,
				char *__path) __THROW;


__END_DECLS

#endif /* rpc/svc.h */
