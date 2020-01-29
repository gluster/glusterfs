#include "metadisp.h"
#include <glusterfs/call-stub.h>

int32_t
metadisp_backend_setattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                             int32_t op_ret, int32_t op_errno,
                             struct iatt *statpre, struct iatt *statpost,
                             dict_t *xdata)

{
    METADISP_TRACE("backend_setattr_cbk");
    if (op_errno == ENOENT) {
        op_errno = ENODATA;
        op_ret = -1;
    }
    STACK_UNWIND_STRICT(setattr, frame, op_ret, op_errno, statpre, statpost,
                        xdata);
    return 0;
}

int32_t
metadisp_backend_setattr_resume(call_frame_t *frame, xlator_t *this, loc_t *loc,
                                struct iatt *stbuf, int32_t valid,
                                dict_t *xdata)

{
    METADISP_TRACE("backend_setattr_resume");
    loc_t backend_loc = {
        0,
    };
    if (build_backend_loc(loc->gfid, loc, &backend_loc)) {
        goto unwind;
    }

    STACK_WIND(frame, metadisp_backend_setattr_cbk, DATA_CHILD(this),
               DATA_CHILD(this)->fops->setattr, &backend_loc, stbuf, valid,
               xdata);
    return 0;

unwind:
    STACK_UNWIND_STRICT(setattr, frame, -1, EINVAL, NULL, NULL, NULL);
    return 0;
}

int32_t
metadisp_setattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct iatt *statpre,
                     struct iatt *statpost, dict_t *xdata)
{
    METADISP_TRACE("%d %d", op_ret, op_errno);
    call_stub_t *stub = NULL;
    stub = cookie;

    if (op_ret != 0) {
        goto unwind;
    }

    if (!IA_ISREG(statpost->ia_type)) {
        goto unwind;
    } else if (!stub) {
        op_errno = EINVAL;
        goto unwind;
    }

    METADISP_TRACE("resuming stub");
    call_resume(stub);
    return 0;
unwind:
    METADISP_TRACE("unwinding %d %d", op_ret, op_errno);
    STACK_UNWIND_STRICT(setattr, frame, op_ret, op_errno, statpre, statpost,
                        xdata);
    if (stub) {
        call_stub_destroy(stub);
    }
    return 0;
}

int32_t
metadisp_setattr(call_frame_t *frame, xlator_t *this, loc_t *loc,
                 struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
    METADISP_TRACE("setattr");
    call_stub_t *stub = NULL;
    stub = fop_setattr_stub(frame, metadisp_backend_setattr_resume, loc, stbuf,
                            valid, xdata);
    STACK_WIND_COOKIE(frame, metadisp_setattr_cbk, stub, METADATA_CHILD(this),
                      METADATA_CHILD(this)->fops->setattr, loc, stbuf, valid,
                      xdata);
    return 0;
}
