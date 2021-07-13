/*
  Copyright (c) 2021 Red Hat, Inc. <https://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <glusterfs/gf-io-legacy.h>

#include <glusterfs/globals.h>
#include <glusterfs/gf-event.h>
#include <glusterfs/timer.h>

static uint64_t gf_io_legacy_seq;

static int32_t
gf_io_legacy_setup(void)
{
    gf_io_legacy_seq = 0;

    return 0;
}

static void
gf_io_legacy_cleanup(void)
{
}

static int32_t
gf_io_legacy_wait(void)
{
    return gf_event_dispatch(global_ctx->event_pool);
}

static void
gf_io_legacy_flush(void)
{
}

static void
gf_io_legacy_cbk(uint64_t id, int32_t res)
{
    uint64_t seq;

    seq = uatomic_add_return(&gf_io_legacy_seq, 1) - 1;

    gf_io_cbk(NULL, seq, id, res);
}

static uint64_t
gf_io_legacy_cancel(uint64_t seq, uint64_t id, gf_io_op_t *op, uint32_t count)
{
    gf_timer_t *timer;

    timer = (gf_timer_t *)(uintptr_t)op->cancel.id;
    if (timer == NULL) {
        gf_io_legacy_cbk(id, -ENOENT);
    } else {
        op = timer->data;
        if (gf_timer_call_cancel(global_ctx, timer) < 0) {
            gf_io_legacy_cbk(id, -EALREADY);
        } else {
            gf_io_legacy_cbk(op->cancel.id, -ECANCELED);
            gf_io_legacy_cbk(id, 0);
        }
    }

    return 0;
}

static uint64_t
gf_io_legacy_callback(uint64_t seq, uint64_t id, gf_io_op_t *op, uint32_t count)
{
    gf_io_legacy_cbk(id, 0);

    return 0;
}

const gf_io_engine_t gf_io_engine_legacy = {
    .name = "legacy",
    .mode = GF_IO_MODE_LEGACY,

    .setup = gf_io_legacy_setup,
    .cleanup = gf_io_legacy_cleanup,
    .wait = gf_io_legacy_wait,

    .worker_setup = NULL,
    .worker_cleanup = NULL,
    .worker_stop = NULL,
    .worker = NULL,

    .flush = gf_io_legacy_flush,

    .cancel = gf_io_legacy_cancel,
    .callback = gf_io_legacy_callback
};
