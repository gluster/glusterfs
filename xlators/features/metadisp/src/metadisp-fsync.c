
#include "metadisp.h"
#include <glusterfs/call-stub.h>

int32_t
metadisp_fsync_resume(call_frame_t *frame, xlator_t *this, fd_t *fd,
                      int32_t flags, dict_t *xdata)
{
    STACK_WIND(frame, default_fsync_cbk, DATA_CHILD(this),
               DATA_CHILD(this)->fops->fsync, fd, flags, xdata);
    return 0;
}

int32_t
metadisp_fsync_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                   struct iatt *postbuf, dict_t *xdata)
{
    call_stub_t *stub = NULL;
    if (cookie) {
        stub = cookie;
    }

    if (op_ret != 0) {
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
    STACK_UNWIND_STRICT(fsync, frame, op_ret, op_errno, prebuf, postbuf, xdata);
    return 0;
}

int32_t
metadisp_fsync(call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t flags,
               dict_t *xdata)
{
    call_stub_t *stub = NULL;
    stub = fop_fsync_stub(frame, metadisp_fsync_resume, fd, flags, xdata);
    STACK_WIND_COOKIE(frame, metadisp_fsync_cbk, stub, METADATA_CHILD(this),
                      METADATA_CHILD(this)->fops->fsync, fd, flags, xdata);
    return 0;
}
