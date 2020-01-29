#include "metadisp.h"
#include <glusterfs/call-stub.h>

/**
 * Lookup, like stat, is a two-step process for grabbing the metadata details
 * as well as the data details.
 */

int32_t
metadisp_backend_lookup_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                            int32_t op_ret, int32_t op_errno, inode_t *inode,
                            struct iatt *buf, dict_t *xdata,
                            struct iatt *postparent)
{
    METADISP_TRACE("backend_lookup_cbk");
    if (op_errno == ENOENT) {
        op_errno = ENODATA;
        op_ret = -1;
    }
    STACK_UNWIND_STRICT(lookup, frame, op_ret, op_errno, inode, buf, xdata,
                        postparent);
    return 0;
}

int32_t
metadisp_backend_lookup_resume(call_frame_t *frame, xlator_t *this, loc_t *loc,
                               dict_t *xdata)
{
    METADISP_TRACE("backend_lookup_resume");
    loc_t backend_loc = {
        0,
    };
    if (build_backend_loc(loc->gfid, loc, &backend_loc)) {
        goto unwind;
    }

    STACK_WIND(frame, metadisp_backend_lookup_cbk, DATA_CHILD(this),
               DATA_CHILD(this)->fops->lookup, &backend_loc, xdata);
    return 0;

unwind:
    STACK_UNWIND_STRICT(lookup, frame, -1, EINVAL, NULL, NULL, NULL, NULL);
    return 0;
}

int32_t
metadisp_lookup_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, inode_t *inode,
                    struct iatt *buf, dict_t *xdata, struct iatt *postparent)
{
    METADISP_TRACE("%d %d", op_ret, op_errno);
    call_stub_t *stub = NULL;
    stub = cookie;

    if (op_ret != 0) {
        goto unwind;
    }

    if (!IA_ISREG(buf->ia_type)) {
        goto unwind;
    } else if (!stub) {
        op_errno = EINVAL;
        goto unwind;
    }

    METADISP_TRACE("resuming stub");

    // memcpy(stub->args.loc.gfid, buf->ia_gfid, sizeof(uuid_t));
    call_resume(stub);
    return 0;
unwind:
    METADISP_TRACE("unwinding %d %d", op_ret, op_errno);
    STACK_UNWIND_STRICT(lookup, frame, op_ret, op_errno, inode, buf, xdata,
                        postparent);
    if (stub) {
        call_stub_destroy(stub);
    }
    return 0;
}

int32_t
metadisp_lookup(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
    METADISP_TRACE("lookup");
    call_stub_t *stub = NULL;
    stub = fop_lookup_stub(frame, metadisp_backend_lookup_resume, loc, xdata);
    STACK_WIND_COOKIE(frame, metadisp_lookup_cbk, stub, METADATA_CHILD(this),
                      METADATA_CHILD(this)->fops->lookup, loc, xdata);
    return 0;
}
