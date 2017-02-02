/*
 * Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
 * This file is part of GlusterFS.
 *
 * This file is licensed to you under your choice of the GNU Lesser
 * General Public License, version 3 or any later version (LGPLv3 or
 * later), or the GNU General Public License, version 2 (GPLv2), in all
 * cases as published by the Free Software Foundation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

//#include "config.h"
#include "glusterfs.h"
#include "globals.h"
#include "glfs-internal.h"
#include "rpc-clnt.h"
#include "protocol-common.h"
#include "xdr-generic.h"
#include "glusterd1-xdr.h"

int done = 0;
int rpc_status;

struct rpc_clnt_procedure gf_attach_actors[GLUSTERD_BRICK_MAXVALUE] = {
        [GLUSTERD_BRICK_NULL] = {"NULL", NULL },
        [GLUSTERD_BRICK_OP]   = {"BRICK_OP", NULL },
};

struct rpc_clnt_program gf_attach_prog = {
        .progname  = "brick operations",
        .prognum   = GD_BRICK_PROGRAM,
        .progver   = GD_BRICK_VERSION,
        .proctable = gf_attach_actors,
        .numproc   = GLUSTERD_BRICK_MAXVALUE,
};

/*
 * In a sane world, the generic RPC layer would be capable of tracking
 * connection status by itself, with no help from us.  It might invoke our
 * callback if we had registered one, but only to provide information.  Sadly,
 * we don't live in that world.  Instead, the callback *must* exist and *must*
 * call rpc_clnt_{set,unset}_connected, because that's the only way those
 * fields get set (with RPC both above and below us on the stack).  If we don't
 * do that, then rpc_clnt_submit doesn't think we're connected even when we
 * are.  It calls the socket code to reconnect, but the socket code tracks this
 * stuff in a sane way so it knows we're connected and returns EINPROGRESS.
 * Then we're stuck, connected but unable to use the connection.  To make it
 * work, we define and register this trivial callback.
 */
int
my_notify (struct rpc_clnt *rpc, void *mydata,
           rpc_clnt_event_t event, void *data)
{
        switch (event) {
        case RPC_CLNT_CONNECT:
                printf ("connected\n");
                rpc_clnt_set_connected (&rpc->conn);
                break;
        case RPC_CLNT_DISCONNECT:
                printf ("disconnected\n");
                rpc_clnt_unset_connected (&rpc->conn);
                break;
        default:
                fprintf (stderr, "unknown RPC event\n");
        }

        return 0;
}

int32_t
my_callback (struct rpc_req *req, struct iovec *iov, int count, void *frame)
{
        rpc_status = req->rpc_status;
        done = 1;
        return 0;
}

/* copied from gd_syncop_submit_request */
int
send_brick_req (xlator_t *this, struct rpc_clnt *rpc, char *path, int op)
{
        int            ret      = -1;
        struct iobuf  *iobuf    = NULL;
        struct iobref *iobref   = NULL;
        struct iovec   iov      = {0, };
        ssize_t        req_size = 0;
        call_frame_t  *frame    = NULL;
        gd1_mgmt_brick_op_req   brick_req;
        void                    *req = &brick_req;
        int                     i;

        brick_req.op = op;
        brick_req.name = path;
        brick_req.input.input_val = NULL;
        brick_req.input.input_len = 0;

        req_size = xdr_sizeof ((xdrproc_t)xdr_gd1_mgmt_brick_op_req, req);
        iobuf = iobuf_get2 (rpc->ctx->iobuf_pool, req_size);
        if (!iobuf)
                goto out;

        iobref = iobref_new ();
        if (!iobref)
                goto out;

        frame = create_frame (this, this->ctx->pool);
        if (!frame)
                goto out;

        iobref_add (iobref, iobuf);

        iov.iov_base = iobuf->ptr;
        iov.iov_len  = iobuf_pagesize (iobuf);

        /* Create the xdr payload */
        ret = xdr_serialize_generic (iov, req,
                                     (xdrproc_t)xdr_gd1_mgmt_brick_op_req);
        if (ret == -1)
                goto out;

        iov.iov_len = ret;

        for (i = 0; i < 60; ++i) {
                if (rpc->conn.connected) {
                        break;
                }
                sleep (1);
        }

        /* Send the msg */
        ret = rpc_clnt_submit (rpc, &gf_attach_prog, op,
                               my_callback, &iov, 1, NULL, 0, iobref, frame,
                               NULL, 0, NULL, 0, NULL);
        if (!ret) {
                for (i = 0; !done && (i < 120); ++i) {
                        sleep (1);
                }
        }

out:

        iobref_unref (iobref);
        iobuf_unref (iobuf);
        STACK_DESTROY (frame->root);

        if (rpc_status != 0) {
                fprintf (stderr, "got error %d on RPC\n", rpc_status);
                return EXIT_FAILURE;
        }

        printf ("OK\n");
        return EXIT_SUCCESS;
}

int
usage (char *prog)
{
        fprintf (stderr, "Usage: %s uds_path volfile_path (to attach)\n",
                 prog);
        fprintf (stderr, "       %s -d uds_path brick_path (to detach)\n",
                 prog);

        return EXIT_FAILURE;
}

int
main (int argc, char *argv[])
{
        glfs_t                  *fs;
        struct rpc_clnt         *rpc;
        dict_t                  *options;
        int                     ret;
        int                     op = GLUSTERD_BRICK_ATTACH;

        for (;;) {
                switch (getopt (argc, argv, "d")) {
                case 'd':
                        op = GLUSTERD_BRICK_TERMINATE;
                        break;
                case -1:
                        goto done_parsing;
                default:
                        return usage (argv[0]);
                }
        }
done_parsing:
        if (optind != (argc - 2)) {
                return usage (argv[0]);
        }

        fs = glfs_new ("gf-attach");
        if (!fs) {
                fprintf (stderr, "glfs_new failed\n");
                return EXIT_FAILURE;
        }

        (void) glfs_set_logging (fs, "/dev/stderr", 7);
        /*
         * This will actually fail because we haven't defined a volume, but
         * it will do enough initialization to get us going.
         */
        (void) glfs_init (fs);

        options = dict_new();
        if (!options) {
                return EXIT_FAILURE;
        }
        ret = dict_set_str (options, "transport-type", "socket");
        if (ret != 0) {
                fprintf (stderr, "failed to set transport type\n");
                return EXIT_FAILURE;
        }
        ret = dict_set_str (options, "transport.address-family", "unix");
        if (ret != 0) {
                fprintf (stderr, "failed to set address family\n");
                return EXIT_FAILURE;
        }
        ret = dict_set_str (options, "transport.socket.connect-path",
                            argv[optind]);
        if (ret != 0) {
                fprintf (stderr, "failed to set connect path\n");
                return EXIT_FAILURE;
        }

        rpc = rpc_clnt_new (options, fs->ctx->master, "gf-attach-rpc", 0);
        if (!rpc) {
                fprintf (stderr, "rpc_clnt_new failed\n");
                return EXIT_FAILURE;
        }

        if (rpc_clnt_register_notify (rpc, my_notify, NULL) != 0) {
                fprintf (stderr, "rpc_clnt_register_notify failed\n");
                return EXIT_FAILURE;
        }

        if (rpc_clnt_start(rpc) != 0) {
                fprintf (stderr, "rpc_clnt_start failed\n");
                return EXIT_FAILURE;
        }

        return send_brick_req (fs->ctx->master, rpc, argv[optind+1], op);
}
