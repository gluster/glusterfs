/*
   Copyright (c) 2010-2013 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include <glusterfs/xlator.h>
#include <glusterfs/glusterfs.h>
#include <glusterfs/syscall.h>
#include <glusterfs/compat-errno.h>

#include "glusterd.h"
#include "glusterd-utils.h"

#include "portmap-xdr.h"
#include "xdr-generic.h"
#include "protocol-common.h"
#include "glusterd-messages.h"
#include "rpcsvc.h"

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>

/* Check if the provided port is free */

static int
pmap_port_isfree(int port)
{
    struct sockaddr_in sin;
    int sock = -1;
    int ret = 0;

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = PF_INET;
    sin.sin_port = hton16(port);

    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock == -1)
        return -1;

    ret = bind(sock, (struct sockaddr *)&sin, sizeof(sin));
    sys_close(sock);

    return (ret == 0) ? 1 : 0;
}

/* Create a new pmap registry */

static struct pmap_registry *
pmap_registry_new(glusterd_conf_t *priv)
{
    struct pmap_registry *pmap = NULL;

    pmap = GF_MALLOC(sizeof(*pmap), gf_common_mt_pmap_reg_t);
    if (!pmap)
        return NULL;

    pmap->base_port = priv->base_port;
    pmap->max_port = priv->max_port;

    return pmap;
}

/* Fetch the already created pamp struct or create new one */

struct pmap_registry *
pmap_registry_get(xlator_t *this)
{
    struct pmap_registry *pmap = NULL;
    glusterd_conf_t *priv = NULL;

    priv = this->private;

    pmap = priv->pmap;
    if (!pmap) {
        pmap = pmap_registry_new(priv);
        if (!pmap)
            return NULL;
        priv->pmap = pmap;
    }

    return pmap;
}

/* Randomly selecting a port number between base_port and max_port,
   then checking if the port is free or not using pmap_port_isfree().
   If the port is free, return the port number
*/

int
pmap_port_alloc(xlator_t *this)
{
    struct pmap_registry *pmap = NULL;
    int p = 0;
    int port = 0;

    pmap = pmap_registry_get(this);

    while (true) {
        p = (rand() % (pmap->max_port - pmap->base_port + 1)) + pmap->base_port;
        if (pmap_port_isfree(p)) {
            port = p;
            break;
        }
    }

    return port;
}

/* Get port number using brickname */

int
__gluster_pmap_portbybrick(rpcsvc_request_t *req)
{
    pmap_port_by_brick_req args = {
        0,
    };
    pmap_port_by_brick_rsp rsp = {
        0,
    };
    int port = 0;
    int ret = -1;
    glusterd_brickinfo_t *brickinfo = NULL;
    xlator_t *this = THIS;

    ret = xdr_to_generic(req->msg[0], &args,
                         (xdrproc_t)xdr_pmap_port_by_brick_req);
    if (ret < 0) {
        req->rpc_err = GARBAGE_ARGS;
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_GARBAGE_ARGS, NULL);
        goto fail;
    }

    ret = glusterd_get_brickinfo(THIS, args.brick, 0, &brickinfo);
    /* get the port number from the brickinfo struct */
    if (!ret) {
        port = brickinfo->port;
        if (!port)
            rsp.op_ret = -1;

        rsp.port = port;
    }

fail:
    glusterd_submit_reply(req, &rsp, NULL, 0, NULL,
                          (xdrproc_t)xdr_pmap_port_by_brick_rsp);
    free(args.brick);  // malloced by xdr

    return 0;
}

int
gluster_pmap_portbybrick(rpcsvc_request_t *req)
{
    return glusterd_big_locked_handler(req, __gluster_pmap_portbybrick);
}

/* Get brickname using the port number */

int
__gluster_pmap_brickbyport(rpcsvc_request_t *req)
{
    pmap_brick_by_port_req args = {
        0,
    };
    pmap_brick_by_port_rsp rsp = {
        0,
    };
    int ret = -1;

    ret = xdr_to_generic(req->msg[0], &args,
                         (xdrproc_t)xdr_pmap_brick_by_port_req);
    if (ret < 0) {
        req->rpc_err = GARBAGE_ARGS;
        gf_smsg(THIS->name, GF_LOG_ERROR, errno, GD_MSG_GARBAGE_ARGS, NULL);
        goto fail;
    }

    ret = glusterd_get_brickinfo(this, NULL, args.port, &brickinfo);
    /* get the brickname from the brickinfo struct */
    if (!ret) {
        gf_strncpy(rsp.brick, brickinfo->path, sizeof(rsp.brick));
        if (!rsp.brick) {
            rsp.op_ret = -1;
            rsp.brick = "";
        }
    }
fail:

    glusterd_submit_reply(req, &rsp, NULL, 0, NULL,
                          (xdrproc_t)xdr_pmap_brick_by_port_rsp);

    return 0;
}

int
gluster_pmap_brickbyport(rpcsvc_request_t *req)
{
    return glusterd_big_locked_handler(req, __gluster_pmap_brickbyport);
}

int
__gluster_pmap_signin(rpcsvc_request_t *req)
{
    pmap_signin_req args = {
        0,
    };
    pmap_signin_rsp rsp = {
        0,
    };
    int ret = -1;
    glusterd_brickinfo_t *brickinfo = NULL;

    ret = xdr_to_generic(req->msg[0], &args, (xdrproc_t)xdr_pmap_signin_req);
    if (ret < 0) {
        req->rpc_err = GARBAGE_ARGS;
        gf_smsg(THIS->name, GF_LOG_ERROR, errno, GD_MSG_GARBAGE_ARGS, NULL);
        goto fail;
    }

    rsp.op_ret = 0;

    ret = glusterd_get_brickinfo(this, args.brick, args.port, &brickinfo);
    /* Update portmap status in brickinfo */
    if (brickinfo)
        brickinfo->port_registered = _gf_true;

fail:
    glusterd_submit_reply(req, &rsp, NULL, 0, NULL,
                          (xdrproc_t)xdr_pmap_signin_rsp);
    free(args.brick);  // malloced by xdr

    return 0;
}

int
gluster_pmap_signin(rpcsvc_request_t *req)
{
    return glusterd_big_locked_handler(req, __gluster_pmap_signin);
}

int
__gluster_pmap_signout(rpcsvc_request_t *req)
{
    pmap_signout_req args = {
        0,
    };
    pmap_signout_rsp rsp = {
        0,
    };
    int ret = -1;
    xlator_t *this = THIS;
    glusterd_conf_t *conf = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    glusterd_brickinfo_t *brickinfo = NULL;
    char pidfile[PATH_MAX] = {0};

    conf = this->private;
    GF_VALIDATE_OR_GOTO(this->name, conf, fail);

    ret = xdr_to_generic(req->msg[0], &args, (xdrproc_t)xdr_pmap_signout_req);
    if (ret < 0) {
        // failed to decode msg;
        req->rpc_err = GARBAGE_ARGS;
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_GARBAGE_ARGS, NULL);
        goto fail;
    }

    ret = glusterd_get_brickinfo(THIS, args.brick, args.port, &brickinfo);
    /* Update portmap status on brickinfo */
    if (!ret)
        brickinfo->port_registered = _gf_false;

    /* Clean up the pidfile for this brick given glusterfsd doesn't clean it
     * any more. This is required to ensure we don't end up with having
     * stale pid files in case a brick is killed from the backend
     */
    ret = glusterd_get_volinfo_from_brick(args.brick, &volinfo);
    if (!ret) {
        if (volinfo && brickinfo) {
            GLUSTERD_GET_BRICK_PIDFILE(pidfile, volinfo, brickinfo, conf);
            sys_unlink(pidfile);

            /* Setting the brick status to GF_BRICK_STOPPED to
             * ensure correct brick status is maintained on the
             * glusterd end when a brick is killed from the
             * backend */
            brickinfo->status = GF_BRICK_STOPPED;

            /* Remove brick from brick process if not already
             * removed in the brick op phase. This situation would
             * arise when the brick is killed explicitly from the
             * backend */
            ret = glusterd_brick_process_remove_brick(brickinfo, NULL);
            if (ret) {
                gf_msg_debug(this->name, 0,
                             "Couldn't remove "
                             "brick %s:%s from brick process",
                             brickinfo->hostname, brickinfo->path);
                /* Ignore 'ret' here since the brick might
                 * have already been deleted in brick op phase
                 */
                ret = 0;
            }
        }
    }

fail:
    glusterd_submit_reply(req, &rsp, NULL, 0, NULL,
                          (xdrproc_t)xdr_pmap_signout_rsp);
    free(args.brick);  // malloced by xdr

    return 0;
}

int
gluster_pmap_signout(rpcsvc_request_t *req)
{
    return glusterd_big_locked_handler(req, __gluster_pmap_signout);
}

static rpcsvc_actor_t gluster_pmap_actors[GF_PMAP_MAXVALUE] = {
    [GF_PMAP_NULL] = {"NULL", NULL, NULL, GF_PMAP_NULL, DRC_NA, 0},
    [GF_PMAP_PORTBYBRICK] = {"PORTBYBRICK", gluster_pmap_portbybrick, NULL,
                             GF_PMAP_PORTBYBRICK, DRC_NA, 0},
    [GF_PMAP_BRICKBYPORT] = {"BRICKBYPORT", gluster_pmap_brickbyport, NULL,
                             GF_PMAP_BRICKBYPORT, DRC_NA, 0},
    [GF_PMAP_SIGNIN] = {"SIGNIN", gluster_pmap_signin, NULL, GF_PMAP_SIGNIN,
                        DRC_NA, 0},
    [GF_PMAP_SIGNOUT] = {"SIGNOUT", gluster_pmap_signout, NULL, GF_PMAP_SIGNOUT,
                         DRC_NA, 0},
};

struct rpcsvc_program gluster_pmap_prog = {
    .progname = "Gluster Portmap",
    .prognum = GLUSTER_PMAP_PROGRAM,
    .progver = GLUSTER_PMAP_VERSION,
    .actors = gluster_pmap_actors,
    .numactors = GF_PMAP_MAXVALUE,
};
