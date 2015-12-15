#pragma fragment CBK_TEMPLATE
int32_t
@FOP_PREFIX@_@NAME@_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
        int32_t op_ret, int32_t op_errno, @UNWIND_PARAMS@)
{
        STACK_UNWIND_STRICT (@NAME@, frame, op_ret, op_errno,
                             @UNWIND_ARGS@);
        return 0;
}

#pragma fragment COMMENT
If you are generating the leaf xlators, remove the STACK_WIND
and replace the @ERROR_ARGS@ to @UNWIND_ARGS@ if necessary

#pragma fragment FOP_TEMPLATE
int32_t
@FOP_PREFIX@_@NAME@ (call_frame_t *frame, xlator_t *this,
        @WIND_PARAMS@)
{
        STACK_WIND (frame, @FOP_PREFIX@_@NAME@_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->@NAME@,
                    @WIND_ARGS@);
        return 0;
err:
        STACK_UNWIND_STRICT (@NAME@, frame, -1, errno,
                             @ERROR_ARGS@);
        return 0;
}

#pragma fragment FUNC_TEMPLATE
@RET_TYPE@
@FOP_PREFIX@_@NAME@ (@FUNC_PARAMS@)
{
        return @RET_VAR@;
}

#pragma fragment CP
/*
 *   Copyright (c) @CURRENT_YEAR@ Red Hat, Inc. <http://www.redhat.com>
 *   This file is part of GlusterFS.
 *
 *   This file is licensed to you under your choice of the GNU Lesser
 *   General Public License, version 3 or any later version (LGPLv3 or
 *   later), or the GNU General Public License, version 2 (GPLv2), in all
 *   cases as published by the Free Software Foundation.
 */

#pragma fragment INCLUDE_IN_SRC_FILE
#include "@XL_NAME@.h"

#pragma fragment INCLUDE_IN_HEADER_FILE
#include "@XL_NAME@-mem-types.h"
#include "@XL_NAME@-messages.h"
#include "glusterfs.h"
#include "xlator.h"
#include "defaults.h"

#pragma fragment XLATOR_METHODS
int32_t
init (xlator_t *this)
{
        return 0;
}

void
fini (xlator_t *this)
{
        return;
}

int32_t
reconfigure (xlator_t *this, dict_t *dict)
{
        return 0;
}

int
notify (xlator_t *this, int event, void *data, ...)
{
        return default_notify (this, event, data);
}

#pragma fragment HEADER_FMT
#ifndef __@HFL_NAME@_H__
#define __@HFL_NAME@_H__

@INCLUDE_SECT@

#endif /* __@HFL_NAME@_H__ */
