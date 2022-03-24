/*
   Copyright (c) 2010-2013 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include <glusterfs/glusterfs.h>
#include <glusterfs/syscall.h>
#include <glusterfs/compat-errno.h>

#include "glusterd-utils.h"

#include "portmap-xdr.h"
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
    sin.sin_port = htobe16(port);

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

    pmap = GF_MALLOC(sizeof(*pmap), gf_gld_mt_pmap_reg_t);
    if (!pmap)
        return NULL;

    CDS_INIT_LIST_HEAD(&pmap->ports);
    pmap->base_port = priv->base_port;
    pmap->max_port = priv->max_port;

    return pmap;
}

/* Fetch the already created pmap struct or create new one */

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
    int i;

    GF_ASSERT(this);

    pmap = pmap_registry_get(this);

    /* coverity[DC.WEAK_CRYPTO] */
    p = (rand() % (pmap->max_port - pmap->base_port + 1)) + pmap->base_port;
    for (i = pmap->base_port; i <= pmap->max_port; i++) {
        if (pmap_port_isfree(p)) {
            return p;
        }
        if (p++ >= pmap->max_port) {
            p = pmap->base_port;
        }
    }

    return 0;
}

/* pmap_assign_port does a pmap_registry_remove followed by pmap_port_alloc,
 * the reason for the former is to ensure we don't end up with stale ports
 */
int
pmap_assign_port(xlator_t *this, int old_port, char *path)
{
    int ret = -1;
    int new_port = 0;

    if (old_port) {
        ret = pmap_port_remove(this, 0, path, NULL, _gf_false);
        if (ret)
            gf_msg(this->name, GF_LOG_WARNING, 0,
                   GD_MSG_PMAP_REGISTRY_REMOVE_FAIL,
                   "Failed to remove old allocated ports");
    }

    new_port = pmap_port_alloc(this);

    return new_port;
}

int
pmap_registry_search(xlator_t *this, char *brickname, gf_boolean_t destroy)
{
    struct pmap_registry *pmap = NULL;
    struct pmap_ports *tmp_port = NULL;
    char *brck = NULL;
    size_t i;

    pmap = pmap_registry_get(this);

    cds_list_for_each_entry(tmp_port, &pmap->ports, port_list)
    {
        brck = tmp_port->brickname;
        for (;;) {
            for (i = 0; brck[i] && !isspace(brck[i]); ++i)
                ;
            if (i == 0 && brck[i] == '\0')
                break;

            if (strncmp(brck, brickname, i) == 0) {
                /*
                 * Without this check, we'd break when brck
                 * is merely a substring of brickname.
                 */
                if (brickname[i] == '\0') {
                    if (destroy)
                        do {
                            *(brck++) = ' ';
                        } while (--i);
                    return tmp_port->port;
                }
            }

            brck += i;

            /*
             * Skip over *any* amount of whitespace, including
             * none (if we're already at the end of the string).
             */
            while (isspace(*brck))
                ++brck;
            /*
             * We're either at the end of the string (which will be
             * handled above strncmp on the next iteration) or at
             * the next non-whitespace substring (which will be
             * handled by strncmp itself).
             */
        }
    }

    return 0;
}

int
pmap_registry_search_by_xprt(xlator_t *this, void *xprt)
{
    struct pmap_registry *pmap = NULL;
    struct pmap_ports *tmp_port = NULL;
    int port = 0;

    pmap = pmap_registry_get(this);
    cds_list_for_each_entry(tmp_port, &pmap->ports, port_list)
    {
        if (tmp_port->xprt == xprt) {
            port = tmp_port->port;
            break;
        }
    }

    return port;
}

int
port_brick_bind(xlator_t *this, int port, char *brickname, void *xprt,
                gf_boolean_t attach_req)
{
    struct pmap_registry *pmap = NULL;
    struct pmap_ports *tmp_port = NULL;
    char *tmp_brick;
    char *new_brickname;
    char *entry;
    size_t brickname_len;
    int ret = -1;
    int found = 0;

    GF_ASSERT(this);

    pmap = pmap_registry_get(this);
    cds_list_for_each_entry(tmp_port, &pmap->ports, port_list)
    {
        if (tmp_port->port == port) {
            ret = 0;
            break;
        }
    }

    if (ret) {
        ret = pmap_add_port_to_list(this, port, brickname, xprt);
        if (ret)
            gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_BRICK_ADD_FAIL,
                   "Failed to add brick to the ports list");
    } else {
        tmp_brick = tmp_port->brickname;
        if (attach_req) {
            brickname_len = strlen(brickname);
            entry = strstr(tmp_brick, brickname);
            while (entry) {
                found = 1;
                if ((entry != tmp_brick) && (entry[-1] != ' '))
                    found = 0;

                if ((entry[brickname_len] != ' ') &&
                    (entry[brickname_len] != '\0'))
                    found = 0;

                if (found)
                    return 0;

                entry = strstr(entry + brickname_len, brickname);
            }
        }
        ret = gf_asprintf(&new_brickname, "%s %s", tmp_brick, brickname);
        if (ret > 0) {
            tmp_port->brickname = new_brickname;
            GF_FREE(tmp_brick);
            ret = 0;
        }
    }

    return ret;
}

/* Allocate memory to store details about the new port i.e, port number,
 * brickname associated with that port, etc */

int
pmap_port_new(xlator_t *this, int port, char *brickname, void *xprt,
              struct pmap_ports **new_port)
{
    struct pmap_ports *tmp_port = NULL;

    tmp_port = GF_MALLOC(sizeof(*tmp_port), gf_gld_mt_pmap_port_t);
    if (!tmp_port)
        return -1;

    CDS_INIT_LIST_HEAD(&tmp_port->port_list);
    tmp_port->brickname = gf_strdup(brickname);
    tmp_port->xprt = xprt;
    tmp_port->port = port;

    *new_port = tmp_port;

    return 0;
}

/* Add the port details to a list in pmap */

int
pmap_add_port_to_list(xlator_t *this, int port, char *brickname, void *xprt)
{
    struct pmap_registry *pmap = NULL;
    struct pmap_ports *new_port = NULL;
    int ret = -1;

    GF_ASSERT(this);

    pmap = pmap_registry_get(this);

    ret = pmap_port_new(this, port, brickname, xprt, &new_port);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_NO_MEMORY,
               "Failed to allocate memory");
        goto out;
    }

    cds_list_add(&new_port->port_list, &pmap->ports);
    ret = 0;

out:
    gf_msg_debug(this->name, 0, "Returning %d", ret);
    return ret;
}

/* Removing the unused port from the list */

int
pmap_port_remove(xlator_t *this, int port, char *brickname, void *xprt,
                 gf_boolean_t brick_disconnect)
{
    struct pmap_registry *pmap = NULL;
    struct pmap_ports *tmp_port = NULL;
    int p = 0;
    int ret = -1;
    char *brick_str;

    GF_ASSERT(this);

    pmap = pmap_registry_get(this);

    if (brickname) {
        p = pmap_registry_search(this, brickname, _gf_true);
        if (p)
            goto remove;
    }

    if (xprt) {
        p = pmap_registry_search_by_xprt(this, xprt);
        if (p)
            goto remove;
    }

    goto out;

remove:
    gf_msg("pmap", GF_LOG_INFO, 0, GD_MSG_BRICK_REMOVE,
           "removing brick %s on port %d", brickname, p);

    /*
     * If all of the brick names have been "whited out" by
     * pmap_registry_search(...,destroy=_gf_true) and there's no xprt either,
     *  then we have nothing left worth saving and can delete the entire entry.
     */
    cds_list_for_each_entry(tmp_port, &pmap->ports, port_list)
    {
        if (tmp_port->port == p) {
            if (xprt && (tmp_port->xprt == xprt))
                tmp_port->xprt = NULL;

            if (brick_disconnect || !(tmp_port->xprt)) {
                /* If the signout call is being triggered by brick disconnect
                 * then clean up all the bricks (in case of brick mux)
                 */
                if (!brick_disconnect) {
                    brick_str = tmp_port->brickname;
                    if (brick_str) {
                        while (*brick_str != '\0') {
                            if (*(brick_str++) != ' ')
                                goto out;
                        }
                    }
                }

                GF_FREE(tmp_port->brickname);
                tmp_port->brickname = NULL;
                tmp_port->xprt = NULL;
                ret = 0;
                break;
            }
        }
    }

    if (!ret) {
        cds_list_del_init(&tmp_port->port_list);
        GF_FREE(tmp_port);
    }

out:
    return 0;
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
    int ret = -1;
    xlator_t *this = THIS;

    ret = xdr_to_generic(req->msg[0], &args,
                         (xdrproc_t)xdr_pmap_port_by_brick_req);
    if (ret < 0) {
        req->rpc_err = GARBAGE_ARGS;
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_GARBAGE_ARGS, NULL);
        goto fail;
    }

    rsp.port = pmap_registry_search(this, args.brick, _gf_false);

    if (!rsp.port)
        rsp.op_ret = -1;

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

/* __gluster_pmap_brickbyport() is currently not called anywhere. Keeping
   the code so that it can be used if anytime needed in future.
   TBD if implemented: Modify pmap_registry_search as well to search for brick
   using port */

int
__gluster_pmap_brickbyport(rpcsvc_request_t *req)
{
    /*pmap_brick_by_port_req args = {
        0,
    };
    pmap_brick_by_port_rsp rsp = {
        0,
    };
    int ret = -1;
    xlator_t *this = THIS;

    ret = xdr_to_generic(req->msg[0], &args,
                         (xdrproc_t)xdr_pmap_brick_by_port_req);
    if (ret < 0) {
        req->rpc_err = GARBAGE_ARGS;
        gf_smsg(THIS->name, GF_LOG_ERROR, errno, GD_MSG_GARBAGE_ARGS, NULL);
        goto fail;
    }

    ret = pmap_registry_search(this, args.port, NULL, _gf_false);
        if(!ret)
            gf_strncpy(rsp.brick, new_brick->brickname, sizeof(rsp.brick));

    if (ret){
        rsp.op_ret = -1;
        rsp.brick = "";
    }

fail:
    glusterd_submit_reply(req, &rsp, NULL, 0, NULL,
                          (xdrproc_t)xdr_pmap_brick_by_port_rsp);
    */

    return 0;
}

int
gluster_pmap_brickbyport(rpcsvc_request_t *req)
{
    return glusterd_big_locked_handler(req, __gluster_pmap_brickbyport);
}

static int
glusterd_get_brickinfo(xlator_t *this, const char *brickname, int port,
                       glusterd_brickinfo_t **brickinfo)
{
    glusterd_conf_t *priv = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    glusterd_brickinfo_t *tmpbrkinfo = NULL;
    glusterd_snap_t *snap = NULL;
    int ret = -1;

    GF_ASSERT(brickname);

    priv = this->private;
    cds_list_for_each_entry(volinfo, &priv->volumes, vol_list)
    {
        cds_list_for_each_entry(tmpbrkinfo, &volinfo->bricks, brick_list)
        {
            if (gf_uuid_compare(tmpbrkinfo->uuid, priv->uuid))
                continue;
            if ((tmpbrkinfo->port == port) &&
                !strcmp(tmpbrkinfo->path, brickname)) {
                *brickinfo = tmpbrkinfo;
                return 0;
            }
        }
    }
    /* In case normal volume is not found, check for snapshot volumes */
    cds_list_for_each_entry(snap, &priv->snapshots, snap_list)
    {
        cds_list_for_each_entry(volinfo, &snap->volumes, vol_list)
        {
            cds_list_for_each_entry(tmpbrkinfo, &volinfo->bricks, brick_list)
            {
                if (gf_uuid_compare(tmpbrkinfo->uuid, priv->uuid))
                    continue;
                if (!strcmp(tmpbrkinfo->path, brickname)) {
                    *brickinfo = tmpbrkinfo;
                    return 0;
                }
            }
        }
    }

    return ret;
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
    xlator_t *this = THIS;
    glusterd_brickinfo_t *brickinfo = NULL;

    ret = xdr_to_generic(req->msg[0], &args, (xdrproc_t)xdr_pmap_signin_req);
    if (ret < 0) {
        req->rpc_err = GARBAGE_ARGS;
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_GARBAGE_ARGS, NULL);
        goto fail;
    }

    rsp.op_ret = port_brick_bind(this, args.port, args.brick, req->trans,
                                 false);

    ret = glusterd_get_brickinfo(this, args.brick, args.port, &brickinfo);
    /* Update portmap status in brickinfo */
    if (!ret)
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

    rsp.op_ret = pmap_port_remove(this, args.port, args.brick, req->trans,
                                  _gf_false);

    ret = glusterd_get_brickinfo(this, args.brick, args.port, &brickinfo);
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
