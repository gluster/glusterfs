/*
  Copyright (c) 2010-2011 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/


#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "xlator.h"
#include "glusterfs.h"
#include "compat-errno.h"

#include "glusterd.h"
#include "glusterd-utils.h"

#include "portmap.h"
#include "protocol-common.h"
#include "rpcsvc.h"

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>


int
pmap_port_isfree (int port)
{
        struct sockaddr_in sin;
        int                sock = -1;
        int                ret = 0;

        memset (&sin, 0, sizeof (sin));
        sin.sin_family = PF_INET;
        sin.sin_port = hton16 (port);

        sock = socket (PF_INET, SOCK_STREAM, 0);
        if (sock == -1)
                return -1;

        ret = bind (sock, (struct sockaddr *)&sin, sizeof (sin));
        close (sock);

        return (ret == 0) ? 1 : 0;
}


struct pmap_registry *
pmap_registry_new (void)
{
        struct pmap_registry *pmap = NULL;
        int                   i = 0;

        pmap = CALLOC (sizeof (*pmap), 1);
        if (!pmap)
                return NULL;

        for (i = 0; i < 65536; i++) {
                if (pmap_port_isfree (i))
                        pmap->ports[i].type = GF_PMAP_PORT_FREE;
                else
                        pmap->ports[i].type = GF_PMAP_PORT_FOREIGN;
        }

        pmap->base_port = GF_DEFAULT_BASE_PORT + 2;
        pmap->last_alloc = GF_DEFAULT_BASE_PORT + 2;

        return pmap;
}


struct pmap_registry *
pmap_registry_get (xlator_t *this)
{
        glusterd_conf_t      *priv = NULL;
        struct pmap_registry *pmap = NULL;

        priv = this->private;

        pmap = priv->pmap;
        if (!pmap) {
                pmap = pmap_registry_new ();
                if (!pmap)
                        return NULL;
                priv->pmap = pmap;
        }

        return pmap;
}


static char*
nextword (char *str)
{
        while (*str && !isspace (*str))
                str++;
        while (*str && isspace (*str))
                str++;

        return str;
}

int
pmap_registry_search (xlator_t *this, const char *brickname,
                      gf_pmap_port_type_t type)
{
        struct pmap_registry *pmap = NULL;
        int                   p = 0;
        char                 *brck = NULL;
        char                 *nbrck = NULL;

        pmap = pmap_registry_get (this);

        for (p = pmap->base_port; p <= pmap->last_alloc; p++) {
                if (!pmap->ports[p].brickname || pmap->ports[p].type != type)
                        continue;

                for (brck = pmap->ports[p].brickname;;) {
                        nbrck = strtail (brck, brickname);
                        if (nbrck && (!*nbrck || isspace (*nbrck)))
                                return p;
                        brck = nextword (brck);
                        if (!*brck)
                                break;
                }
        }

        return 0;
}

int
pmap_registry_search_by_xprt (xlator_t *this, void *xprt,
                              gf_pmap_port_type_t type)
{
        struct pmap_registry *pmap = NULL;
        int                   p    = 0;
        int                   port = 0;

        pmap = pmap_registry_get (this);

        for (p = pmap->base_port; p <= pmap->last_alloc; p++) {
                if (!pmap->ports[p].xprt)
                        continue;
                if (pmap->ports[p].xprt == xprt &&
                    pmap->ports[p].type == type) {
                        port = p;
                        break;
                }
        }

        return port;
}


char *
pmap_registry_search_by_port (xlator_t *this, int port)
{
        struct pmap_registry *pmap = NULL;
        char *brickname = NULL;

        if (port > 65535)
                goto out;

        pmap = pmap_registry_get (this);

        if (pmap->ports[port].type == GF_PMAP_PORT_BRICKSERVER)
                brickname = pmap->ports[port].brickname;

out:
        return brickname;
}


int
pmap_registry_alloc (xlator_t *this)
{
        struct pmap_registry *pmap = NULL;
        int                   p = 0;
        int                   port = 0;

        pmap = pmap_registry_get (this);

        for (p = pmap->last_alloc; p < 65535; p++) {
                if (pmap->ports[p].type != GF_PMAP_PORT_FREE)
                        continue;

                if (pmap_port_isfree (p)) {
                        pmap->ports[p].type = GF_PMAP_PORT_LEASED;
                        port = p;
                        break;
                }
        }

        if (port)
                pmap->last_alloc = port;

        return port;
}

int
pmap_registry_bind (xlator_t *this, int port, const char *brickname,
                    gf_pmap_port_type_t type, void *xprt)
{
        struct pmap_registry *pmap = NULL;
        int                   p = 0;

        pmap = pmap_registry_get (this);

        if (port > 65535)
                goto out;

        p = port;
        pmap->ports[p].type = type;
        if (pmap->ports[p].brickname)
                free (pmap->ports[p].brickname);
        pmap->ports[p].brickname = strdup (brickname);
        pmap->ports[p].type = type;
        pmap->ports[p].xprt = xprt;

        gf_log ("pmap", GF_LOG_INFO, "adding brick %s on port %d",
                brickname, port);

        if (pmap->last_alloc < p)
                pmap->last_alloc = p;
out:
        return 0;
}

int
pmap_registry_remove (xlator_t *this, int port, const char *brickname,
                      gf_pmap_port_type_t type, void *xprt)
{
        struct pmap_registry *pmap = NULL;
        int                   p = 0;
        glusterd_conf_t      *priv = NULL;

        priv = this->private;
        pmap = priv->pmap;
        if (!pmap)
                goto out;

        if (port) {
                if (port > 65535)
                        goto out;

                p = port;
                goto remove;
        }

        if (brickname && strchr (brickname, '/')) {
                p = pmap_registry_search (this, brickname, type);
                if (p)
                        goto remove;
        }

        if (xprt) {
                p = pmap_registry_search_by_xprt (this, xprt, type);
                if (p)
                        goto remove;
        }

        goto out;
remove:
        gf_log ("pmap", GF_LOG_INFO, "removing brick %s on port %d",
                pmap->ports[p].brickname, p);

        if (pmap->ports[p].brickname)
                free (pmap->ports[p].brickname);

        pmap->ports[p].brickname = NULL;
        pmap->ports[p].xprt = NULL;

out:
        return 0;
}


typedef ssize_t (*gfs_serialize_t) (struct iovec outmsg, void *data);


static int
xdr_to_glusterfs_req (rpcsvc_request_t *req, void *arg, gfs_serialize_t sfunc)
{
        int                     ret = -1;

        if (!req)
                return -1;

        ret = sfunc (req->msg[0], arg);

        if (ret > 0)
                ret = 0;

        return ret;
}


int
gluster_pmap_portbybrick (rpcsvc_request_t *req)
{
        pmap_port_by_brick_req    args = {0,};
        pmap_port_by_brick_rsp    rsp  = {0,};
        char                     *brick = NULL;
        int                       port = 0;

        if (xdr_to_glusterfs_req (req, &args, xdr_to_pmap_port_by_brick_req)) {
                req->rpc_err = GARBAGE_ARGS;
                goto fail;
        }

        brick = args.brick;

        port = pmap_registry_search (THIS, brick, GF_PMAP_PORT_BRICKSERVER);

        if (!port)
                rsp.op_ret = -1;

        rsp.port = port;

fail:
        glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                               (gd_serialize_t)xdr_from_pmap_port_by_brick_rsp,
                               (xdrproc_t)xdr_pmap_port_by_brick_rsp);
        if (args.brick)
                free (args.brick);//malloced by xdr

        return 0;
}


int
gluster_pmap_brickbyport (rpcsvc_request_t *req)
{
        pmap_brick_by_port_req    args = {0,};
        pmap_brick_by_port_rsp    rsp  = {0,};

        if (xdr_to_glusterfs_req (req, &args, xdr_to_pmap_brick_by_port_req)) {
                req->rpc_err = GARBAGE_ARGS;
                goto fail;
        }

        rsp.brick = pmap_registry_search_by_port (THIS, args.port);
        if (!rsp.brick) {
                rsp.op_ret = -1;
                rsp.brick = "";
        }
fail:

        glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                               (gd_serialize_t)xdr_from_pmap_brick_by_port_rsp,
                               (xdrproc_t)xdr_pmap_brick_by_port_rsp);

        return 0;
}

static int
glusterd_brick_update_signin (glusterd_brickinfo_t *brickinfo,
                              gf_boolean_t value)
{
        brickinfo->signed_in = value;

        return 0;
}

int
gluster_pmap_signup (rpcsvc_request_t *req)
{
        pmap_signup_req    args = {0,};
        pmap_signup_rsp    rsp  = {0,};


        if (xdr_to_glusterfs_req (req, &args, xdr_to_pmap_signup_req)) {
                req->rpc_err = GARBAGE_ARGS;
                goto fail;
        }

        rsp.op_ret = pmap_registry_bind (THIS, args.port, args.brick,
                                         GF_PMAP_PORT_BRICKSERVER, req->trans);

fail:
        glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                               (gd_serialize_t)xdr_from_pmap_signup_rsp,
                               (xdrproc_t)xdr_pmap_signup_rsp);
        if (args.brick)
                free (args.brick);//malloced by xdr

        return 0;
}

int
gluster_pmap_signin (rpcsvc_request_t *req)
{
        pmap_signin_req    args = {0,};
        pmap_signin_rsp    rsp  = {0,};
        glusterd_brickinfo_t *brickinfo = NULL;
        int                ret = -1;

        if (xdr_to_glusterfs_req (req, &args, xdr_to_pmap_signin_req)) {
                req->rpc_err = GARBAGE_ARGS;
                goto fail;
        }

        rsp.op_ret = pmap_registry_bind (THIS, args.port, args.brick,
                                         GF_PMAP_PORT_BRICKSERVER, req->trans);

        ret = glusterd_get_brickinfo (THIS, args.brick, args.port, _gf_true,
                                      &brickinfo);
fail:
        glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                               (gd_serialize_t)xdr_from_pmap_signin_rsp,
                               (xdrproc_t)xdr_pmap_signin_rsp);
        if (args.brick)
                free (args.brick);//malloced by xdr

        if (!ret)
                glusterd_brick_update_signin (brickinfo, _gf_true);

        return 0;
}



int
gluster_pmap_signout (rpcsvc_request_t *req)
{
        pmap_signout_req    args = {0,};
        pmap_signout_rsp    rsp  = {0,};
        int                 ret = -1;
        glusterd_brickinfo_t *brickinfo = NULL;

        if (xdr_to_glusterfs_req (req, &args, xdr_to_pmap_signout_req)) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto fail;
        }

        rsp.op_ret = pmap_registry_remove (THIS, args.port, args.brick,
                                           GF_PMAP_PORT_BRICKSERVER, req->trans);

        ret = glusterd_get_brickinfo (THIS, args.brick, args.port, _gf_true,
                                      &brickinfo);
fail:
        glusterd_submit_reply (req, &rsp, NULL, 0, NULL,
                               (gd_serialize_t)xdr_from_pmap_signout_rsp,
                               (xdrproc_t)xdr_pmap_signout_rsp);
        if (args.brick)
                free (args.brick);//malloced by xdr

        if (!ret)
                glusterd_brick_update_signin (brickinfo, _gf_false);

        return 0;
}

rpcsvc_actor_t gluster_pmap_actors[] = {
        [GF_PMAP_NULL] = {"NULL", GF_HNDSK_NULL, NULL, NULL, NULL },
        [GF_PMAP_PORTBYBRICK] = {"PORTBYBRICK", GF_PMAP_PORTBYBRICK,
                                 gluster_pmap_portbybrick, NULL, NULL },
        [GF_PMAP_BRICKBYPORT] = {"BRICKBYPORT", GF_PMAP_BRICKBYPORT,
                                 gluster_pmap_brickbyport, NULL, NULL },
        [GF_PMAP_SIGNIN] = {"SIGNIN", GF_PMAP_SIGNIN,
                              gluster_pmap_signin, NULL, NULL },
        [GF_PMAP_SIGNOUT] = {"SIGNOUT", GF_PMAP_SIGNOUT,
                              gluster_pmap_signout, NULL, NULL },
        [GF_PMAP_SIGNUP] = {"SIGNUP", GF_PMAP_SIGNUP,
                              gluster_pmap_signup, NULL, NULL },
};


struct rpcsvc_program gluster_pmap_prog = {
        .progname  = "Gluster Portmap",
        .prognum   = GLUSTER_PMAP_PROGRAM,
        .progver   = GLUSTER_PMAP_VERSION,
        .actors    = gluster_pmap_actors,
        .numactors = GF_PMAP_MAXVALUE,
};
