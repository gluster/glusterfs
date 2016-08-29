/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __DC_H__
#define __DC_H__

#include "defaults.h"
#include "xlator.h"
#include "call-stub.h"
#include "decompounder-mem-types.h"
#include "decompounder-messages.h"

typedef struct {
        compound_args_t *compound_req;
        compound_args_cbk_t *compound_rsp;
        int     counter;
        int     length;
} dc_local_t;

#define DC_STACK_UNWIND(frame, op_ret, op_errno, rsp, xdata) do {\
                dc_local_t      *__local = NULL;                      \
                                                                      \
                if (frame) {                                          \
                        __local = frame->local;                       \
                        frame->local = NULL;                          \
                }                                                     \
                STACK_UNWIND_STRICT (compound, frame, op_ret, op_errno,    \
                                     (void *)rsp, xdata);             \
                if (__local) {                                        \
                        dc_local_cleanup (__local);                   \
                        mem_put (__local);                            \
                }                                                     \
        } while (0)

int32_t
dc_compound_fop_wind (call_frame_t *frame, xlator_t *this);

void
dc_local_cleanup (dc_local_t *local);

#define DC_FOP_RESPONSE_STORE_AND_WIND_NEXT(fop, frame, op_ret, op_errno, params ...) do {      \
        dc_local_t              *__local        = frame->local;                                 \
        xlator_t                *__this         = frame->this;                                  \
        int                     __ret           = 0;                                            \
        int                     __counter       = __local->counter;                             \
        compound_args_cbk_t     *__compound_rsp = __local->compound_rsp;                        \
        default_args_cbk_t      *__fop_rsp      = &__local->compound_rsp->rsp_list[__counter];  \
                                                                                                \
        if (op_ret < 0) {                                                               \
                gf_msg (__this->name, GF_LOG_ERROR, op_errno, DC_MSG_ERROR_RECEIVED,    \
                        "fop number %d failed. Unwinding.", __counter+1);               \
                args_##fop##_cbk_store (__fop_rsp,                                      \
                                        op_ret, op_errno, params);                      \
                /*TODO : Fill the rest of the responses to -1 or NULL*/                 \
                DC_STACK_UNWIND (frame, op_ret, op_errno,                     \
                                 (void *)__compound_rsp, NULL);                         \
        } else {                                                                        \
                args_##fop##_cbk_store (__fop_rsp,                                      \
                                        op_ret, op_errno, params);                      \
                __local->counter++;                                                     \
                __ret = dc_compound_fop_wind (frame, __this);                           \
                if (__ret < 0) {                                                        \
                        DC_STACK_UNWIND (frame, -1, -__ret,                   \
                                         (void *)__compound_rsp, NULL);                 \
                }                                                                       \
        }                                                                               \
        } while (0)
#endif /* DC_H__ */
