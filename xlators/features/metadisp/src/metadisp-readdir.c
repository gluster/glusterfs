#include "metadisp.h"

/**
 * With a change to the posix xlator, readdir and readdirp are shockingly
 * simple.
 *
 * The issue with separating the backend data of the files
 * with the metadata is that readdirs must now read from multiple sources
 * to coalesce the directory entries.
 *
 * The way we do this is to tell the METADATA_CHILD that when it's
 * running readdirp, each file entry should have a stat wound to
 * 'stat-source-of-truth'.
 *
 * see metadisp_stat for how it handles winds _from_posix.
 */

int32_t
metadisp_readdir(call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
                 off_t off, dict_t *xdata)
{
    METADISP_TRACE(".");
    /*
     * Always use readdirp, even if the original was readdir. Why? Because NFS.
     * There are multiple translations between Gluster, UNIX, and NFS stat
     * structures in that path. One of them uses the type etc. from the stat
     * structure, which is only filled in by readdirp. If we use readdir, the
     * entries do actually go all the way back to the client and are visible in
     * getdents, but then the readdir throws them away because of the
     * uninitialized type.
     */
    GF_UNUSED int32_t ret;
    if (!xdata) {
        xdata = dict_new();
    }

    // ret = dict_set_int32 (xdata, "list-xattr", 1);

    // I'm my own source of truth!
    ret = dict_set_static_ptr(xdata, "stat-source-of-truth", (void *)this);

    STACK_WIND(frame, default_readdirp_cbk, METADATA_CHILD(this),
               METADATA_CHILD(this)->fops->readdirp, fd, size, off, xdata);

    return 0;
}

int32_t
metadisp_readdirp(call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
                  off_t off, dict_t *xdata)
{
    METADISP_TRACE(".");
    if (!xdata) {
        xdata = dict_new();
    }
    GF_UNUSED int32_t ret;
    // ret = dict_set_int32 (xdata, "list-xattr", 1);

    // I'm my own source of truth!
    ret = dict_set_static_ptr(xdata, "stat-source-of-truth", (void *)this);

    STACK_WIND(frame, default_readdirp_cbk, METADATA_CHILD(this),
               METADATA_CHILD(this)->fops->readdirp, fd, size, off, xdata);
    return 0;
}
