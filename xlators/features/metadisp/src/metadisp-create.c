#include "metadisp.h"
#include <glusterfs/call-stub.h>

/**
 * Create, like stat, is a two-step process. We send a create
 * to the METADATA_CHILD, then send another create to the DATA_CHILD.
 *
 * We do the metadata child first to ensure that the ACLs are enforced.
 */

int32_t
metadisp_create_dentry_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                           int32_t op_ret, int32_t op_errno, fd_t *fd,
                           inode_t *inode, struct iatt *buf,
                           struct iatt *preparent, struct iatt *postparent,
                           dict_t *xdata)
{
    STACK_UNWIND_STRICT(create, frame, op_ret, op_errno, fd, inode, buf,
                        preparent, postparent, xdata);
    return 0;
}

int32_t
metadisp_create_resume(call_frame_t *frame, xlator_t *this, loc_t *loc,
                       int32_t flags, mode_t mode, mode_t umask, fd_t *fd,
                       dict_t *xdata)
{
    // create the backend data inode
    STACK_WIND(frame, metadisp_create_dentry_cbk, DATA_CHILD(this),
               DATA_CHILD(this)->fops->create, loc, flags, mode, umask, fd,
               xdata);
    return 0;
}

int32_t
metadisp_create_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, fd_t *fd, inode_t *inode,
                    struct iatt *buf, struct iatt *preparent,
                    struct iatt *postparent, dict_t *xdata)
{
    METADISP_TRACE("%d %d", op_ret, op_errno);
    call_stub_t *stub = cookie;
    if (op_ret != 0) {
        STACK_UNWIND_STRICT(create, frame, op_ret, op_errno, fd, inode, buf,
                            preparent, postparent, xdata);
        return 0;
    }

    if (stub == NULL) {
        goto unwind;
    }

    if (stub->poison) {
        call_stub_destroy(stub);
        return 0;
    }

    call_resume(stub);
    return 0;

unwind:
    STACK_UNWIND_STRICT(create, frame, -1, EINVAL, NULL, NULL, NULL, NULL, NULL,
                        NULL);
    return 0;
}

int32_t
metadisp_create(call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
                mode_t mode, mode_t umask, fd_t *fd, dict_t *xdata)
{
    METADISP_TRACE(".");

    loc_t backend_loc = {
        0,
    };
    call_stub_t *stub = NULL;
    uuid_t *gfid_req = NULL;

    RESOLVE_GFID_REQ(xdata, gfid_req, out);

    if (build_backend_loc(*gfid_req, loc, &backend_loc)) {
        goto unwind;
    }

    frame->local = loc;

    stub = fop_create_stub(frame, metadisp_create_resume, &backend_loc, flags,
                           mode, umask, fd, xdata);

    STACK_WIND_COOKIE(frame, metadisp_create_cbk, stub, METADATA_CHILD(this),
                      METADATA_CHILD(this)->fops->create, loc, flags, mode,
                      umask, fd, xdata);
    return 0;

unwind:
    STACK_UNWIND_STRICT(create, frame, -1, EINVAL, NULL, NULL, NULL, NULL, NULL,
                        NULL);
    return 0;
out:
    return -1;
}
