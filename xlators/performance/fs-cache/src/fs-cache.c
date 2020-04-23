/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/
#include <glusterfs/call-stub.h>
#include <glusterfs/defaults.h>
#include <glusterfs/glusterfs.h>
#include <glusterfs/logging.h>
#include <glusterfs/dict.h>
#include <glusterfs/xlator.h>
#include "fs-cache.h"
#include "fsc-mem-types.h"
#include <signal.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <glusterfs/locking.h>
#include <glusterfs/timespec.h>


int32_t
fsc_lookup_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, inode_t *inode,
               struct iatt *stbuf, dict_t *xdata, struct iatt *postparent)
{
    fsc_local_t *local = NULL;

    if (op_ret != 0)
        goto out;

    local = frame->local;
    if (local == NULL) {
        op_ret = -1;
        op_errno = EINVAL;
        goto out;
    }

    if (!this || !this->private) {
        op_ret = -1;
        op_errno = EINVAL;
        goto out;
    }

    fsc_inode_update(this, inode, (char *)local->file_loc.path, stbuf);

out:
    if (frame->local != NULL) {
        local = frame->local;
        loc_wipe(&local->file_loc);
    }

    STACK_UNWIND_STRICT(lookup, frame, op_ret, op_errno, inode, stbuf, xdata,
                        postparent);
    return 0;
}

int32_t
fsc_lookup(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
    fsc_local_t *local = NULL;
    int32_t op_errno = -1, ret = -1;

    local = mem_get0(this->local_pool);
    if (local == NULL) {
        op_errno = ENOMEM;
        gf_msg(this->name, GF_LOG_ERROR, 0, FS_CACHE_MSG_ERROR,
               "out of memory");
        goto unwind;
    }

    ret = loc_copy(&local->file_loc, loc);
    if (ret != 0) {
        op_errno = ENOMEM;
        gf_msg(this->name, GF_LOG_ERROR, 0, FS_CACHE_MSG_ERROR,
               "out of memory");
        goto unwind;
    }

    frame->local = local;

    STACK_WIND(frame, fsc_lookup_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->lookup, loc, xdata);

    return 0;

unwind:
    if (local != NULL) {
        loc_wipe(&local->file_loc);
        mem_put(local);
    }

    STACK_UNWIND_STRICT(lookup, frame, -1, op_errno, NULL, NULL, NULL, NULL);

    return 0;
}



int
fsc_readdirp_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int op_ret,
                 int op_errno, gf_dirent_t *entries, dict_t *xdata)
{
    gf_dirent_t *entry = NULL;
    char *path = NULL;
    fd_t *fd = NULL;

    fd = frame->local;
    frame->local = NULL;

    if (op_ret <= 0)
        goto unwind;

    list_for_each_entry(entry, &entries->list, list)
    {
        inode_path(fd->inode, entry->d_name, &path);
        fsc_inode_update(this, entry->inode, path, &entry->d_stat);
        GF_FREE(path);
        path = NULL;
    }

unwind:
    STACK_UNWIND_STRICT(readdirp, frame, op_ret, op_errno, entries, xdata);

    return 0;
}


int
fsc_readdirp(call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
             off_t offset, dict_t *dict)
{
    frame->local = fd;

    STACK_WIND(frame, fsc_readdirp_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->readdirp, fd, size, offset, dict);

    return 0;
}



int
fsc_readv_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, struct iovec *vector, int32_t count,
              struct iatt *stbuf, struct iobref *iobref, dict_t *xdata)
{
    int32_t ret = -1;
    int32_t retval = -1;
    int32_t tmp = 0;
    off_t internal_off = 0;
    int32_t idx = 0;
    int32_t old_flags = 0;
    int32_t is_cancel_dio = 0;
    int32_t vec_len = 0;
    int32_t iobref_len = 0;
    int32_t max_buf_size = 0;
    int32_t real_write_len = 0;

    fsc_conf_t *conf = NULL;
    fsc_local_t *local = NULL;
    fsc_inode_t *fsc_inode = NULL;
    char *alloc_buf = NULL;
    char *buf = NULL;

    local = frame->local;
    GF_ASSERT(local);
    conf = this->private;
    fsc_inode = local->inode;
    GF_ASSERT(fsc_inode);

    if (op_ret < 0) {
        goto out;
    }

    if (!conf->is_enable) {
        goto out;
    }

    if (conf->disk_space_full == 1) {
        goto out;
    }

    /* try write to local*/
    for (idx = 0; idx < count; idx++) {
        tmp = vector[idx].iov_len;
        vec_len += tmp;
        if (max_buf_size < tmp)
            max_buf_size = tmp;
    }
    iobref_len = iobref_size(iobref);

    fsc_inode_lock(fsc_inode);
    ret = fsc_inode_open_for_write(this, fsc_inode);
    if (ret < 0) {
        goto unlock;
    }

    gf_msg(this->name, GF_LOG_TRACE, 0, FS_CACHE_MSG_TRACE,
           "readv_cbk on gfid=%s, "
           "fd=%d,offset=%" PRIu64 " size=%" GF_PRI_SIZET
           ", "
           "vec_len=%d,iobref_len=%d,ia_size=(%zu/%zu),local_size=%zu",
           uuid_utoa(fsc_inode->inode->gfid), fsc_inode->fsc_fd, local->offset, local->size,
           vec_len, iobref_len, stbuf->ia_size, fsc_inode->ia_size, fsc_inode->fsc_size);

    ret = fsc_block_is_cache(this, fsc_inode, local->offset, vec_len);
    if ( ret == 0) {
        gf_msg(this->name, GF_LOG_INFO, 0, FS_CACHE_MSG_INFO,
               "has cached local file=%s,fd=%d,offset=%zu,size=%d ", fsc_inode->local_path, fsc_inode->fsc_fd, local->offset, vec_len);
        goto unlock;
    }

    /*start write*/
    alloc_buf = fsc_page_aligned_alloc(max_buf_size, &buf);
    if (!alloc_buf) {
        goto unlock;
    }

    if ((vec_len % 512) != 0) {
        old_flags = fcntl (fsc_inode->fsc_fd, F_GETFL);
        if ((old_flags & O_DIRECT)) {
            is_cancel_dio = 1;
            gf_msg(this->name, GF_LOG_INFO, 0, FS_CACHE_MSG_INFO,
                   "write remove O_DIRECT file=%s,fd=%d", fsc_inode->local_path, fsc_inode->fsc_fd);

            if (fcntl (fsc_inode->fsc_fd, F_SETFL, old_flags & ~O_DIRECT) != 0) {
                gf_msg(this->name, GF_LOG_ERROR, errno, FS_CACHE_MSG_ERROR,
                       "write remove O_DIRECT failed file=%s,fd=%d", fsc_inode->local_path, fsc_inode->fsc_fd);
            }
        }

    }

    internal_off = local->offset;
    for (idx = 0; idx < count; idx++) {
        tmp = vector[idx].iov_len;
        memcpy(buf, vector[idx].iov_base, tmp);
        retval = sys_pwrite(fsc_inode->fsc_fd, buf, tmp,
                            internal_off);
        if (retval == -1) {
            gf_msg(this->name, GF_LOG_ERROR, errno, FS_CACHE_MSG_ERROR,
                   "write local file=%s,fd=%d,offset=%zu,size=%d,failed.", fsc_inode->local_path, fsc_inode->fsc_fd, internal_off, tmp);
            goto unlock;
        }

        gf_msg(this->name, GF_LOG_TRACE, 0, FS_CACHE_MSG_TRACE,
               "write local file=%s, offset=%zu, size=%d ", fsc_inode->local_path, internal_off, retval);
        real_write_len += retval;
        internal_off += retval;
    }

    if (is_cancel_dio == 1) {
        gf_msg(this->name, GF_LOG_INFO, 0, FS_CACHE_MSG_INFO,
               "write recover O_DIRECT file=%s,fd=%d", fsc_inode->local_path, fsc_inode->fsc_fd);
        if (fcntl (fsc_inode->fsc_fd, F_SETFL, old_flags | O_DIRECT) != 0) {
            gf_msg(this->name, GF_LOG_ERROR, errno, FS_CACHE_MSG_ERROR,
                   "write recover O_DIRECT failed file=%s,fd=%d", fsc_inode->local_path, fsc_inode->fsc_fd);
        }
    }

    fsc_block_add(this, fsc_inode, local->offset, real_write_len);
    fsc_inode->fsc_size += real_write_len;
    if (fsc_inode->fsc_size >= fsc_inode->ia_size) {

        gf_msg(this->name, GF_LOG_INFO, 0, FS_CACHE_MSG_INFO,
               "write local finished fd=%d, file=%s", fsc_inode->fsc_fd, fsc_inode->local_path);
        fsc_block_flush(this, fsc_inode);

        if (conf->direct_io_read == 1) {
            old_flags = fcntl (fsc_inode->fsc_fd, F_GETFL);
            if ((old_flags & O_DIRECT)) {
                goto unlock;
            }

            if (sys_fdatasync(fsc_inode->fsc_fd) == -1) {
                gf_msg(this->name, GF_LOG_ERROR, errno, FS_CACHE_MSG_ERROR,
                       "fdatasync failed fd=%d, file=%s", fsc_inode->fsc_fd, fsc_inode->local_path);
            }
            gf_msg(this->name, GF_LOG_INFO, 0, FS_CACHE_MSG_INFO,
                   "clear pagecache fdatasync fd=%d, file=%s", fsc_inode->fsc_fd, fsc_inode->local_path);

            if (fcntl(fsc_inode->fsc_fd, F_SETFL, O_DIRECT | O_RDWR) != 0) {
                gf_msg(this->name, GF_LOG_ERROR, errno, FS_CACHE_MSG_ERROR,
                       "set directIO read failed fd=%d,file=%s", fsc_inode->fsc_fd, fsc_inode->local_path);
            }
            gf_msg(this->name, GF_LOG_INFO, 0, FS_CACHE_MSG_INFO,
                   "clear pagecache set directIO for read fd=%d, file=%s", fsc_inode->fsc_fd, fsc_inode->local_path);

            if (posix_fadvise(fsc_inode->fsc_fd, 0, 0, POSIX_FADV_DONTNEED) == -1) {
                gf_msg(this->name, GF_LOG_ERROR, errno, FS_CACHE_MSG_ERROR,
                       "clear pagecache and set directIO read failed fd=%d, file=%s", fsc_inode->fsc_fd, fsc_inode->local_path);
            }
            gf_msg(this->name, GF_LOG_INFO, 0, FS_CACHE_MSG_INFO,
                   "clear pagecache fd=%d, file=%s", fsc_inode->fsc_fd, fsc_inode->local_path);
        }
    }

unlock:
    fsc_inode_unlock(fsc_inode);

out:
    if (alloc_buf) {
        GF_FREE(alloc_buf);
    }

    mem_put(local);
    frame->local = NULL;

    STACK_UNWIND_STRICT(readv, frame, op_ret, op_errno, vector, count, stbuf,
                        iobref, xdata);

    return 0;
}


/*
 * fsc_readv -
 *
 * @frame:
 * @this:
 * @fd:
 * @size:
 * @offset:
 *
 */
int32_t
fsc_readv(call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
          off_t offset, uint32_t flags, dict_t *xdata)
{
    uint64_t tmp_fsc_inode = 0;
    fsc_inode_t *fsc_inode = NULL;
    fsc_local_t *local = NULL;
    int ret = -1;
    fsc_conf_t *conf = NULL;
    int32_t op_errno = EINVAL;

    conf = this->private;

    if (!this) {
        goto out;
    }

    if (!conf->is_enable) {
        STACK_WIND_TAIL(frame, FIRST_CHILD(this),
                        FIRST_CHILD(this)->fops->readv, fd, size, offset, flags,
                        xdata);
        return 0;
    }

    if (conf->disk_space_full == 1) {
        STACK_WIND_TAIL(frame, FIRST_CHILD(this),
                        FIRST_CHILD(this)->fops->readv, fd, size, offset, flags,
                        xdata);
        return 0;
    }

    inode_ctx_get(fd->inode, this, &tmp_fsc_inode);
    fsc_inode = (fsc_inode_t *)(long)tmp_fsc_inode;
    if (!fsc_inode) {
        gf_msg(this->name, GF_LOG_TRACE, 0, FS_CACHE_MSG_TRACE,
               "fsc_inode readv not find fsc_inode gfid=(%s)", uuid_utoa(fd->inode->gfid));
        /* fs caching disabled, go ahead with normal readv */
        STACK_WIND_TAIL(frame, FIRST_CHILD(this),
                        FIRST_CHILD(this)->fops->readv, fd, size, offset, flags,
                        xdata);
        return 0;
    }

    if (!conf) {
        gf_msg(this->name, GF_LOG_ERROR, EINVAL,
               FS_CACHE_MSG_ENFORCEMENT_FAILED, "fsc_local  is null");
        op_errno = EINVAL;
        goto out;
    }

    /* try read from local file*/
    ret = fsc_inode_read(fsc_inode, frame, this, fd, size, offset, flags, xdata);
    if (ret >= 0) {
        return 0;
    }

    local = mem_get0(this->local_pool);
    if (local == NULL) {
        gf_msg(this->name, GF_LOG_ERROR, ENOMEM, FS_CACHE_MSG_ERROR,
               "out of memory");
        op_errno = ENOMEM;
        goto out;
    }

    frame->local = local;
    local->pending_offset = offset;
    local->pending_size = size;
    local->offset = offset;
    local->size = size;
    local->inode = fsc_inode;

    gf_msg(this->name, GF_LOG_DEBUG, 0, FS_CACHE_MSG_DEBUG,
           "NEW REQ (%p) offset "
           "= %" PRId64 " && size = %" GF_PRI_SIZET "",
           frame, offset, size);

    STACK_WIND(frame, fsc_readv_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->readv, fd,
               size, offset, 0, xdata);

    return 0;

out:
    STACK_UNWIND_STRICT(readv, frame, -1, op_errno, NULL, 0, NULL, NULL, NULL);
    return 0;
}


/*
 * fsc_forget -
 *
 * @frame:
 * @this:
 * @inode:
 *
 */
int32_t
fsc_forget(xlator_t *this, inode_t *inode)
{
    uint64_t tmp_fsc_inode = 0;
    fsc_inode_t *fsc_inode = NULL;
    fsc_conf_t *conf = this->private;

    inode_ctx_get(inode, this, &tmp_fsc_inode);
    fsc_inode = (fsc_inode_t *)(long)tmp_fsc_inode;
    if (fsc_inode) {
        gf_msg(this->name, GF_LOG_INFO, 0, FS_CACHE_MSG_INFO,
               "fsc_forget fsc inode %d,path=%s", fsc_inode->fsc_fd, fsc_inode->local_path);
        fsc_inodes_list_lock(conf);
        {
            conf->inodes_count--;
            list_del_init(&fsc_inode->inode_list);
        }
        fsc_inodes_list_unlock(conf);
        fsc_inode_destroy(fsc_inode, 0);
    }
    return 0;
}

int
fsc_priv_dump(xlator_t *this)
{
    fsc_conf_t *conf = NULL;
    char key_prefix[GF_DUMP_MAX_BUF_LEN];

    if (!this)
        return 0;

    conf = this->private;
    if (!conf)
        return 0;

    snprintf(key_prefix, GF_DUMP_MAX_BUF_LEN, "%s.%s", this->type, this->name);

    gf_proc_dump_add_section("%s", key_prefix);

    gf_proc_dump_write("cache-dir", "%s", conf->cache_dir);

    return 0;
}


int32_t
mem_acct_init(xlator_t *this)
{
    int ret = -1;

    if (!this)
        return ret;

    ret = xlator_mem_acct_init(this, gf_fsc_mt_end + 1);

    if (ret != 0) {
        gf_msg(this->name, GF_LOG_ERROR, ENOMEM, FS_CACHE_MSG_ERROR,
               "Memory accounting init failed");
        return ret;
    }

    return ret;
}


static uint32_t
is_match(const char *path, const char *pattern)
{
    int32_t ret = 0;

    ret = fnmatch(pattern, path, FNM_NOESCAPE);

    return (ret == 0);
}

gf_boolean_t
fsc_check_filter(fsc_conf_t *conf, const char *path)
{
    int ii;
    gf_boolean_t is_set = _gf_false;
    char* pattern = NULL;
    for (ii = 0; ii < 3; ++ii) {
        pattern = conf->filters.pattern[ii];
        if (*pattern == '\0') {
            continue;
        }
        is_set = _gf_true;
        if (is_match(path, pattern)) {
            return _gf_true;
        }
    }
    return !is_set;
}

void fsc_print_filters(xlator_t *this, fsc_conf_t *conf) {
    int ii;
    for (ii = 0; ii < 3; ++ii) {
        gf_msg(this->name, GF_LOG_INFO, 0, FS_CACHE_MSG_INFO,
               "filter pattern%d=%s", ii, conf->filters.pattern[ii]);
    }
}

void fsc_resolve_filters(xlator_t *this, fsc_conf_t *conf, char* str) {
    size_t len;
    size_t unit_len = 0;
    char tmp[512] = {0};
    int32_t idx = 0;
    char *p = NULL;
    char *dst;
    if (!str) {
        return;
    }

    conf->filters.pattern[0][0] = '\0';
    conf->filters.pattern[1][0] = '\0';
    conf->filters.pattern[2][0] = '\0';

    if (strlen(str) == 0) {
        return;
    }

    if (strcmp(str, "null") == 0) {
        return;
    }


    snprintf(tmp, 512 - 1, "%s", str);
    len = strlen(tmp);
    if (tmp[len - 1] != ';') {
        tmp[len] = ';';
        tmp[len + 1] = 0;
    }

    dst = & conf->filters.pattern[idx][0];
    for (p = tmp; *p; p++) {
        if (*p == ';') {
            *dst = '\0';
            idx++;
            if (idx == 3) {
                break;
            }
            /* next */
            dst = & conf->filters.pattern[idx][0];
            unit_len = 0;
        } else {
            *dst = *p;
            dst++;
            unit_len++;
            if (unit_len == FSC_CACHE_PATTERN_LEN - 1) {
                *dst = '\0';
                /* too long */
                break;
            }
        }
    }
    fsc_print_filters(this, conf);
}

int
reconfigure(xlator_t *this, dict_t *options)
{
    fsc_conf_t *conf = NULL;
    char* tmp = NULL;
    int ret = -1;
    if (!this || !this->private)
        goto out;

    conf = this->private;

    GF_OPTION_RECONF("fsc-disk-reserve", conf->disk_reserve, options, uint32, out);
    GF_OPTION_RECONF("fsc-resycle-idle-inode", conf->resycle_idle_inode, options, uint32, out);
    GF_OPTION_RECONF("fsc-time-idle-inode", conf->time_idle_inode, options, uint32, out);

    GF_OPTION_RECONF("fsc-direct-io-read", conf->direct_io_read, options, uint32, out);
    GF_OPTION_RECONF("fsc-direct-io-write", conf->direct_io_write, options, uint32, out);

    GF_OPTION_RECONF("fsc-min-file-size", conf->min_file_size, options, size_uint64, out);

    GF_OPTION_RECONF("fsc-cache-filter", tmp, options, str, out);
    fsc_resolve_filters(this, conf, tmp);
    ret = 0;
out:
    gf_msg(this->name, GF_LOG_INFO, 0, FS_CACHE_MSG_INFO,
           "fs-cache xlator=%p reconfigure options cache_dir=%s,disk_reserve=%d,resycle_idle_inode=%d,time_idle_inode=%d,direct_io_read=%d,direct_io_write=%d,min_file_size=%zu",
           this, conf->cache_dir, conf->disk_reserve, conf->resycle_idle_inode, conf->time_idle_inode, conf->direct_io_read, conf->direct_io_write, conf->min_file_size);

    return ret;
}

int
init(xlator_t *this)
{
    fsc_conf_t *conf = NULL;
    int ret = -1;
    char* tmp = NULL;

    struct stat fstatbuf = {
        0,
    };
    if (!this->children || this->children->next) {
        gf_msg("fs-cache", GF_LOG_ERROR, 0,
               FS_CACHE_MSG_ERROR,
               "FATAL: iot not configured "
               "with exactly one child");
        goto out;
    }

    if (!this->parents) {
        gf_msg(this->name, GF_LOG_WARNING, 0, FS_CACHE_MSG_ERROR,
               "dangling volume. check volfile ");
    }

    conf = (void *)GF_CALLOC(1, sizeof(*conf), gf_fsc_mt_flag);
    if (conf == NULL) {
        gf_msg(this->name, GF_LOG_ERROR, ENOMEM, FS_CACHE_MSG_ERROR,
               "out of memory");
        goto out;
    }
    GF_OPTION_INIT("fsc-cache-dir", conf->cache_dir, str, out);
    GF_OPTION_INIT("fsc-disk-reserve", conf->disk_reserve, uint32, out);
    GF_OPTION_INIT("fsc-resycle-idle-inode", conf->resycle_idle_inode, uint32, out);
    GF_OPTION_INIT("fsc-time-idle-inode", conf->time_idle_inode, uint32, out);

    GF_OPTION_INIT("fsc-direct-io-read", conf->direct_io_read, uint32, out);
    GF_OPTION_INIT("fsc-direct-io-write", conf->direct_io_write, uint32, out);
    GF_OPTION_INIT("fsc-min-file-size", conf->min_file_size, size_uint64, out);

    conf->is_enable = _gf_true;
    ret = sys_stat(conf->cache_dir, &fstatbuf);
    if (ret == -1) {
        gf_msg("fs-cache", GF_LOG_ERROR, 0, FS_CACHE_MSG_ERROR,
               "init not find path=(%s)",   conf->cache_dir);
        conf->is_enable = _gf_false;
    }

    GF_OPTION_INIT("fsc-cache-filter", tmp, str, out);
    fsc_resolve_filters(this, conf, tmp);


    gf_msg(this->name, GF_LOG_INFO, 0, FS_CACHE_MSG_INFO,
           "fs-cache xlator=%p init options cache_dir=%s,disk_reserve=%d,resycle_idle_inode=%d,time_idle_inode=%d,direct_io_read=%d,direct_io_write=%d,min_file_size=%zu",
           this, conf->cache_dir, conf->disk_reserve, conf->resycle_idle_inode, conf->time_idle_inode, conf->direct_io_read, conf->direct_io_write, conf->min_file_size);

    INIT_LIST_HEAD(&conf->inodes);
    pthread_mutex_init(&conf->inodes_lock, NULL);
    pthread_mutex_init(&conf->aux_lock, NULL);

    this->local_pool = mem_pool_new(fsc_local_t, 64);
    if (!this->local_pool) {
        ret = -1;
        gf_msg(this->name, GF_LOG_ERROR, ENOMEM, FS_CACHE_MSG_ERROR,
               "failed to create local_t's memory pool");
        goto out;
    }

    conf->this = this;
    this->private = conf;

    ret = fsc_spawn_aux_thread(this);
    if (ret)
        goto out;

    ret = 0;
out:
    if (ret)
        GF_FREE(conf);

    return ret;
}

int
fsc_notify(xlator_t *this, int event, void *data, ...)
{
    int ret = 0;
    fsc_conf_t *conf = NULL;
    fsc_inode_t *curr = NULL, *tmp = NULL;
    conf = this->private;
    struct list_head clear_list;
    gf_msg(this->name, GF_LOG_DEBUG, 0, FS_CACHE_MSG_DEBUG,
           "fs-cache xlator=%p notify %d", this, event);
    switch (event) {
    case GF_EVENT_PARENT_DOWN: {
        gf_msg(this->name, GF_LOG_INFO, 0, FS_CACHE_MSG_INFO,
               "fs-cache xlator=%p down", this);

        INIT_LIST_HEAD(&clear_list);

        fsc_inodes_list_lock(conf);
        {
            list_replace_init(&conf->inodes, &clear_list);
            conf->inodes_count = 0;
        }
        fsc_inodes_list_unlock(conf);

        list_for_each_entry_safe(curr, tmp, &clear_list, inode_list)
        {
            list_del(&curr->inode_list);
            fsc_inode_destroy(curr, 3);
        }

        if (conf->aux_thread) {
            conf->aux_thread_active = _gf_false;
            (void)gf_thread_cleanup_xint(conf->aux_thread);
            conf->aux_thread = 0;
        }
    }
    default:
        break;
    }

    if (default_notify(this, event, data) != 0)
        ret = -1;

    return ret;
}



void
fini(xlator_t *this)
{
    fsc_conf_t *conf = this->private;
    gf_msg(this->name, GF_LOG_INFO, ENOMEM, FS_CACHE_MSG_INFO,
           "fs-cache xlator=%p fini", this);

    if (!conf)
        return;

    if (conf->aux_thread) {
        conf->aux_thread_active = _gf_false;
        (void)gf_thread_cleanup_xint(conf->aux_thread);
        conf->aux_thread = 0;
    }

    pthread_mutex_destroy(&conf->inodes_lock);
    pthread_mutex_destroy(&conf->aux_lock);
    GF_FREE(conf);

    this->private = NULL;
    return;
}

struct xlator_fops fops = {
    .readv = fsc_readv,
    .lookup = fsc_lookup,
    .readdirp = fsc_readdirp,
};

struct xlator_dumpops dumpops = {
    .priv = fsc_priv_dump,
};

struct xlator_cbks cbks = {
    .forget = fsc_forget,
};

struct volume_options options[] = {
    {
        .key = {"fsc-cache-dir"},
        .type = GF_OPTION_TYPE_STR,
        .op_version = {1},
        .tags = {"fsc"},
        .flags = OPT_FLAG_CLIENT_OPT | OPT_FLAG_SETTABLE | OPT_FLAG_DOC,
        .default_value = "/data/gfs/cache",
        .description = "root path of cache files",
    },
    {
        .key = {"fsc-cache-filter"},
        .type = GF_OPTION_TYPE_STR,
        .op_version = {1},
        .tags = {"fsc"},
        .flags = OPT_FLAG_CLIENT_OPT | OPT_FLAG_SETTABLE | OPT_FLAG_DOC,
        .default_value = "",
        .description = "file cache local disk only when match the pattern, the length of pattern string limit 128, split by semicolon",
    },

    {
        .key = {"fsc-disk-reserve"},
        .type = GF_OPTION_TYPE_INT,
        .min = 10,
        .max = 90,
        .default_value = "10",
        .tags = {"fsc"},
        .description = "Size of free space in disk.",
        .op_version = {1},
        .flags = OPT_FLAG_CLIENT_OPT | OPT_FLAG_SETTABLE | OPT_FLAG_DOC
    },
    {
        .key = {"fsc-resycle-idle-inode"},
        .type = GF_OPTION_TYPE_INT,
        .min = 0,
        .max = 1,
        .default_value = "1",
        .tags = {"fsc"},
        .description = "enable or disable resycle idle fsc-node",
        .op_version = {1},
        .flags = OPT_FLAG_CLIENT_OPT | OPT_FLAG_SETTABLE | OPT_FLAG_DOC
    },

    {
        .key = {"fsc-direct-io-read"},
        .type = GF_OPTION_TYPE_INT,
        .min = 0,
        .max = 1,
        .default_value = "1",
        .tags = {"fsc"},
        .description = "enable or disable Direct IO when read date from disk",
        .op_version = {1},
        .flags = OPT_FLAG_CLIENT_OPT | OPT_FLAG_SETTABLE | OPT_FLAG_DOC
    },

    {
        .key = {"fsc-direct-io-write"},
        .type = GF_OPTION_TYPE_INT,
        .min = 0,
        .max = 1,
        .default_value = "0",
        .tags = {"fsc"},
        .description = "enable or disable Direct IO when write to disk",
        .op_version = {1},
        .flags = OPT_FLAG_CLIENT_OPT | OPT_FLAG_SETTABLE | OPT_FLAG_DOC
    },

    {
        .key = {"fsc-time-idle-inode"},
        .type = GF_OPTION_TYPE_INT,
        .min = 600,
        .max = 31536000,
        .default_value = "3600",
        .tags = {"fsc"},
        .description = "inode will be in idle status, when inode is not accessed within this time (sec)",
        .op_version = {1},
        .flags = OPT_FLAG_CLIENT_OPT | OPT_FLAG_SETTABLE | OPT_FLAG_DOC
    },

    {.key = {"fsc-min-file-size"},
     .type = GF_OPTION_TYPE_SIZET,
     .default_value = "0",
     .description = "Minimum file size which would be cached by the "
                    "fs-cache translator.",
     .op_version = {1},
     .flags = OPT_FLAG_CLIENT_OPT | OPT_FLAG_SETTABLE | OPT_FLAG_DOC
    },

    {.key = {NULL}},
};

xlator_api_t xlator_api = {
    .init = init,
    .fini = fini,
    .notify = fsc_notify,
    .reconfigure = reconfigure,
    .mem_acct_init = mem_acct_init,
    .op_version = {1}, /* Present from the initial version */
    .dumpops = &dumpops,
    .fops = &fops,
    .cbks = &cbks,
    .options = options,
    .identifier = "fs-cache",
    .category = GF_MAINTAINED,
};
