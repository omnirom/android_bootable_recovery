/* @(#)svc_auth.c       2.4 88/08/15 4.0 RPCSRC */
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
static char sccsid[] = "@(#)svc_auth.c 1.19 87/08/11 Copyr 1984 Sun Micro";
#endif

/*
 * svc_auth.c, Server-side rpc authenticator interface.
 *
 * Copyright (C) 1984, Sun Microsystems, Inc.
 */

#include <rpc/rpc.h>
#include <rpc/svc.h>
#include <rpc/svc_auth.h>

/*
 * svcauthsw is the bdevsw of server side authentication.
 *
 * Server side authenticators are called from authenticate by
 * using the client auth struct flavor field to index into svcauthsw.
 * The server auth flavors must implement a routine that looks
 * like:
 *
 *      enum auth_stat
 *      flavorx_auth(rqst, msg)
 *              register struct svc_req *rqst;
 *              register struct rpc_msg *msg;
 *
 */

static enum auth_stat _svcauth_null (struct svc_req *, struct rpc_msg *);
				/* no authentication */
extern enum auth_stat _svcauth_unix (struct svc_req *, struct rpc_msg *);
				/* unix style (uid, gids) */
extern enum auth_stat _svcauth_short (struct svc_req *, struct rpc_msg *);
				/* short hand unix style */
#ifdef CONFIG_AUTH_DES
extern enum auth_stat _svcauth_des (struct svc_req *, struct rpc_msg *);
				/* des style */
#endif

static const struct
  {
    enum auth_stat (*authenticator) (struct svc_req *, struct rpc_msg *);
  }
svcauthsw[] =
{
  { _svcauth_null },		/* AUTH_NULL */
  { _svcauth_unix },		/* AUTH_UNIX */
  { _svcauth_short },		/* AUTH_SHORT */
#ifdef CONFIG_AUTH_DES
  { _svcauth_des }		/* AUTH_DES */
#endif
};
#define	AUTH_MAX	3	/* HIGHEST AUTH NUMBER */


/*
 * The call rpc message, msg has been obtained from the wire.  The msg contains
 * the raw form of credentials and verifiers.  authenticate returns AUTH_OK
 * if the msg is successfully authenticated.  If AUTH_OK then the routine also
 * does the following things:
 * set rqst->rq_xprt->verf to the appropriate response verifier;
 * sets rqst->rq_client_cred to the "cooked" form of the credentials.
 *
 * NB: rqst->rq_cxprt->verf must be pre-allocated;
 * its length is set appropriately.
 *
 * The caller still owns and is responsible for msg->u.cmb.cred and
 * msg->u.cmb.verf.  The authentication system retains ownership of
 * rqst->rq_client_cred, the cooked credentials.
 *
 * There is an assumption that any flavour less than AUTH_NULL is
 * invalid.
 */
enum auth_stat
_authenticate (register struct svc_req *rqst, struct rpc_msg *msg)
{
  register int cred_flavor;

  rqst->rq_cred = msg->rm_call.cb_cred;
  rqst->rq_xprt->xp_verf.oa_flavor = _null_auth.oa_flavor;
  rqst->rq_xprt->xp_verf.oa_length = 0;
  cred_flavor = rqst->rq_cred.oa_flavor;
  if ((cred_flavor <= AUTH_MAX) && (cred_flavor >= AUTH_NULL))
    return (*(svcauthsw[cred_flavor].authenticator)) (rqst, msg);

  return AUTH_REJECTEDCRED;
}
libc_hidden_def(_authenticate)

static enum auth_stat
_svcauth_null (struct svc_req *rqst attribute_unused, struct rpc_msg *msg attribute_unused)
{
  return AUTH_OK;
}
