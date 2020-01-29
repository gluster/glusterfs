#include <glusterfs/call-stub.h>
#include "metadisp.h"

int32_t
metadisp_open_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, fd_t *fd, dict_t *xdata)
{
    METADISP_TRACE("got open results %d %d", op_ret, op_errno);

    call_stub_t *stub = NULL;
    if (cookie) {
        stub = cookie;
    }

    if (op_ret != 0) {
        goto unwind;
    }

    if (!stub) {
        goto unwind;
    }

    if (stub->poison) {
        call_stub_destroy(stub);
        stub = NULL;
        return 0;
    }

    call_resume(stub);
    return 0;

unwind:
    if (stub) {
        call_stub_destroy(stub);
    }
    STACK_UNWIND_STRICT(open, frame, op_ret, op_errno, fd, xdata);
    return 0;
}

int32_t
metadisp_open_resume(call_frame_t *frame, xlator_t *this, loc_t *loc,
                     int32_t flags, fd_t *fd, dict_t *xdata)
{
    STACK_WIND_COOKIE(frame, metadisp_open_cbk, NULL, DATA_CHILD(this),
                      DATA_CHILD(this)->fops->open, loc, flags, fd, xdata);
    return 0;
}

int32_t
metadisp_open(call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
              fd_t *fd, dict_t *xdata)
{
    call_stub_t *stub = NULL;
    loc_t backend_loc = {
        0,
    };

    if (build_backend_loc(loc->gfid, loc, &backend_loc)) {
        goto unwind;
    }

    stub = fop_open_stub(frame, metadisp_open_resume, &backend_loc, flags, fd,
                         xdata);
    STACK_WIND_COOKIE(frame, metadisp_open_cbk, stub, METADATA_CHILD(this),
                      METADATA_CHILD(this)->fops->open, loc, flags, fd, xdata);
    return 0;
unwind:
    STACK_UNWIND_STRICT(open, frame, -1, EINVAL, NULL, NULL);
    return 0;
}
