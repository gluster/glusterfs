#include "metadisp.h"
#include <glusterfs/call-stub.h>

/**
 * The stat flow in METADISP is complicated because we must
 * do ensure a few things:
 *    1. stat, on the path within the metadata layer,
 *       MUST get the backend FD of the data layer.
 *        --- we wind to the metadata layer, then the data layer.
 *
 *    2. the metadata layer MUST be able to ask the data
 *       layer for stat information.
 *        --- this is 'syncop-internal-from-posix'
 *
 *    3. when the metadata exists BUT the data is missing,
 *       we MUST mark the backend file as bad and heal it.
 */

int32_t
metadisp_stat_backend_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno, struct iatt *buf,
                          dict_t *xdata)
{
    METADISP_TRACE("got backend stat results %d %d", op_ret, op_errno);
    if (op_errno == ENOENT) {
        STACK_UNWIND_STRICT(open, frame, -1, ENODATA, NULL, NULL);
        return 0;
    }
    STACK_UNWIND_STRICT(stat, frame, op_ret, op_errno, buf, xdata);
    return 0;
}

int32_t
metadisp_stat_resume(call_frame_t *frame, xlator_t *this, loc_t *loc,
                     dict_t *xdata)
{
    METADISP_TRACE("winding stat to path %s", loc->path);
    if (gf_uuid_is_null(loc->gfid)) {
        METADISP_TRACE("bad object, sending EUCLEAN");
        STACK_UNWIND_STRICT(open, frame, -1, EUCLEAN, NULL, NULL);
        return 0;
    }

    STACK_WIND(frame, metadisp_stat_backend_cbk, SECOND_CHILD(this),
               SECOND_CHILD(this)->fops->stat, loc, xdata);
    return 0;
}

int32_t
metadisp_stat_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct iatt *buf,
                  dict_t *xdata)
{
    call_stub_t *stub = NULL;

    METADISP_TRACE("got stat results %d %d", op_ret, op_errno);

    if (cookie) {
        stub = cookie;
    }

    if (op_ret != 0) {
        goto unwind;
    }

    // only use the stub for the files
    if (!IA_ISREG(buf->ia_type)) {
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
    STACK_UNWIND_STRICT(stat, frame, op_ret, op_errno, buf, xdata);
    return 0;
}

int32_t
metadisp_stat(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
    call_stub_t *stub = NULL;
    int32_t ret = 0;
    loc_t backend_loc = {
        0,
    };
    METADISP_FILTER_ROOT(stat, loc, xdata);

    if (build_backend_loc(loc->gfid, loc, &backend_loc)) {
        goto unwind;
    }

    if (dict_get_int32(xdata, "syncop-internal-from-posix", &ret) == 0) {
        // if we've just been sent a stat from posix, then we know
        // that we must send down a stat for a file to the second child.
        //
        // that means we can skip the stat for the first child and just
        // send to the data disk.
        METADISP_TRACE("got syncop-internal-from-posix");
        STACK_WIND(frame, default_stat_cbk, DATA_CHILD(this),
                   DATA_CHILD(this)->fops->stat, &backend_loc, xdata);
        return 0;
    }

    // we do not know if the request is for a file, folder, etc. wind
    // to first child to find out.
    stub = fop_stat_stub(frame, metadisp_stat_resume, &backend_loc, xdata);
    METADISP_TRACE("winding stat to first child %s", loc->path);
    STACK_WIND_COOKIE(frame, metadisp_stat_cbk, stub, METADATA_CHILD(this),
                      METADATA_CHILD(this)->fops->stat, loc, xdata);
    return 0;
unwind:
    STACK_UNWIND_STRICT(stat, frame, -1, EINVAL, NULL, NULL);
    return 0;
}
