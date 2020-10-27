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

#include <glusterfs/glusterfs.h>
#include "glfs-internal.h"
#include "rpc-clnt.h"
#include "protocol-common.h"
#include "xdr-generic.h"
#include "glusterd1-xdr.h"

/* In seconds */
#define CONNECT_TIMEOUT 60
#define REPLY_TIMEOUT 120

int done = 0;
int rpc_status;

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

struct rpc_clnt_procedure gf_attach_actors[GLUSTERD_BRICK_MAXVALUE] = {
    [GLUSTERD_BRICK_NULL] = {"NULL", NULL},
    [GLUSTERD_BRICK_OP] = {"BRICK_OP", NULL},
};

struct rpc_clnt_program gf_attach_prog = {
    .progname = "brick operations",
    .prognum = GD_BRICK_PROGRAM,
    .progver = GD_BRICK_VERSION,
    .proctable = gf_attach_actors,
    .numproc = GLUSTERD_BRICK_MAXVALUE,
};

int32_t
my_callback(struct rpc_req *req, struct iovec *iov, int count, void *frame)
{
    pthread_mutex_lock(&mutex);
    rpc_status = req->rpc_status;
    done = 1;
    /* Signal main thread which is the only waiter */
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&mutex);
    return 0;
}

/* copied from gd_syncop_submit_request */
int
send_brick_req(xlator_t *this, struct rpc_clnt *rpc, char *path, int op)
{
    int ret = -1;
    struct timespec ts;
    struct iobuf *iobuf = NULL;
    struct iobref *iobref = NULL;
    struct iovec iov = {
        0,
    };
    ssize_t req_size = 0;
    call_frame_t *frame = NULL;
    gd1_mgmt_brick_op_req brick_req;
    void *req = &brick_req;

    brick_req.op = op;
    brick_req.name = path;
    brick_req.input.input_val = NULL;
    brick_req.input.input_len = 0;
    brick_req.dict.dict_val = NULL;
    brick_req.dict.dict_len = 0;

    req_size = xdr_sizeof((xdrproc_t)xdr_gd1_mgmt_brick_op_req, req);
    iobuf = iobuf_get2(rpc->ctx->iobuf_pool, req_size);
    if (!iobuf)
        goto out;

    iobref = iobref_new();
    if (!iobref)
        goto out;

    iobref_add(iobref, iobuf);

    iov.iov_base = iobuf->ptr;
    iov.iov_len = iobuf_pagesize(iobuf);

    /* Create the xdr payload */
    ret = xdr_serialize_generic(iov, req, (xdrproc_t)xdr_gd1_mgmt_brick_op_req);
    if (ret == -1)
        goto out;

    iov.iov_len = ret;

    /* Wait for connection */
    timespec_now_realtime(&ts);
    ts.tv_sec += CONNECT_TIMEOUT;
    pthread_mutex_lock(&rpc->conn.lock);
    {
        while (!rpc->conn.connected)
            if (pthread_cond_timedwait(&rpc->conn.cond, &rpc->conn.lock, &ts) ==
                ETIMEDOUT) {
                fprintf(stderr, "timeout waiting for RPC connection\n");
                pthread_mutex_unlock(&rpc->conn.lock);
                return EXIT_FAILURE;
            }
    }
    pthread_mutex_unlock(&rpc->conn.lock);

    frame = create_frame(this, this->ctx->pool);
    if (!frame) {
        ret = -1;
        goto out;
    }

    /* Send the msg */
    ret = rpc_clnt_submit(rpc, &gf_attach_prog, op, my_callback, &iov, 1, NULL,
                          0, iobref, frame, NULL, 0, NULL, 0, NULL);
    if (!ret) {
        /* OK, wait for callback */
        timespec_now_realtime(&ts);
        ts.tv_sec += REPLY_TIMEOUT;
        pthread_mutex_lock(&mutex);
        {
            while (!done)
                if (pthread_cond_timedwait(&cond, &mutex, &ts) == ETIMEDOUT) {
                    fprintf(stderr, "timeout waiting for RPC reply\n");
                    pthread_mutex_unlock(&mutex);
                    return EXIT_FAILURE;
                }
        }
        pthread_mutex_unlock(&mutex);
    }

out:

    iobref_unref(iobref);
    iobuf_unref(iobuf);
    if (frame)
        STACK_DESTROY(frame->root);

    if (rpc_status != 0) {
        fprintf(stderr, "got error %d on RPC\n", rpc_status);
        return EXIT_FAILURE;
    }

    printf("OK\n");
    return EXIT_SUCCESS;
}

int
usage(char *prog)
{
    fprintf(stderr, "Usage: %s uds_path volfile_path (to attach)\n", prog);
    fprintf(stderr, "       %s -d uds_path brick_path (to detach)\n", prog);

    return EXIT_FAILURE;
}

int
main(int argc, char *argv[])
{
    glfs_t *fs;
    struct rpc_clnt *rpc;
    dict_t *options;
    int ret;
    int op = GLUSTERD_BRICK_ATTACH;

    for (;;) {
        switch (getopt(argc, argv, "d")) {
            case 'd':
                op = GLUSTERD_BRICK_TERMINATE;
                break;
            case -1:
                goto done_parsing;
            default:
                return usage(argv[0]);
        }
    }
done_parsing:
    if (optind != (argc - 2)) {
        return usage(argv[0]);
    }

    fs = glfs_new("gf-attach");
    if (!fs) {
        fprintf(stderr, "glfs_new failed\n");
        return EXIT_FAILURE;
    }

    (void)glfs_set_logging(fs, "/dev/stderr", 7);
    /*
     * This will actually fail because we haven't defined a volume, but
     * it will do enough initialization to get us going.
     */
    (void)glfs_init(fs);

    options = dict_new();
    if (!options) {
        return EXIT_FAILURE;
    }
    ret = dict_set_str(options, "transport-type", "socket");
    if (ret != 0) {
        fprintf(stderr, "failed to set transport type\n");
        return EXIT_FAILURE;
    }
    ret = dict_set_str(options, "transport.address-family", "unix");
    if (ret != 0) {
        fprintf(stderr, "failed to set address family\n");
        return EXIT_FAILURE;
    }
    ret = dict_set_str(options, "transport.socket.connect-path", argv[optind]);
    if (ret != 0) {
        fprintf(stderr, "failed to set connect path\n");
        return EXIT_FAILURE;
    }

    rpc = rpc_clnt_new(options, fs->ctx->primary, "gf-attach-rpc", 0);
    if (!rpc) {
        fprintf(stderr, "rpc_clnt_new failed\n");
        return EXIT_FAILURE;
    }

    if (rpc_clnt_register_notify(rpc, NULL, NULL) != 0) {
        fprintf(stderr, "rpc_clnt_register_notify failed\n");
        return EXIT_FAILURE;
    }

    if (rpc_clnt_start(rpc) != 0) {
        fprintf(stderr, "rpc_clnt_start failed\n");
        return EXIT_FAILURE;
    }

    return send_brick_req(fs->ctx->primary, rpc, argv[optind + 1], op);
}
