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

#define BLOCK_BASE_LEN 10
#define BLOCK_ATTR_NAME "user.gfs.cache.block"

int32_t
fsc_block_to_str(xlator_t *this, fsc_inode_t *inode, char* buff, int32_t len)
{
    if (!inode->write_block) {
        return 0;
    }
    int32_t idx = 0;
    int32_t offset = 0;
    fsc_block_t* cur = NULL;
    fsc_block_t* p = inode->write_block;
    for (idx = 0; idx < inode->write_block_len; ++idx) {
        cur = p + idx;
        if ( cur->start == 0 && cur->end == 0) {
            /* idle block*/
            continue;
        }
        offset += snprintf(buff + offset, len - offset, "%" PRIu64 ":%" PRIu64 " ",
                           cur->start, cur->end);
    }
    return offset;
}

int32_t
fsc_block_dump(xlator_t *this, fsc_inode_t *inode, char* hint)
{
    gf_loglevel_t existing_level = GF_LOG_NONE;
    char* buff = NULL;
    int32_t len = inode->write_block_len * 42;

    existing_level = this->loglevel ? this->loglevel : this->ctx->log.loglevel;
    if (existing_level < GF_LOG_TRACE ) {
        return 0;
    }

    buff = GF_CALLOC(1, len, gf_fsc_mt_fsc_block_dump_t);
    if (!buff) {
        return -1;
    }
    fsc_block_to_str(this, inode, buff, len);
    gf_msg(this->name, GF_LOG_INFO, 0, FS_CACHE_MSG_TRACE,
           "fsc_block_dump %s fd=%d, path=%s,block=%s", hint, inode->fsc_fd, inode->local_path, buff);
    GF_FREE(buff);
    return 0;
}


int32_t
fsc_block_merge(xlator_t *this, fsc_inode_t *inode, int32_t merge_idx, int32_t merge_direct)
{
    int32_t idx = 0;
    fsc_block_t* cur = NULL;
    fsc_block_t* p = inode->write_block;
    fsc_block_t* merge_src = inode->write_block + merge_idx;
    fsc_block_t* merge_target = NULL;
    gf_boolean_t is_merged = _gf_false;
    do {

        if (merge_target) {
            merge_src->start = min(merge_src->start, merge_target->start);
            merge_src->end = max(merge_src->end, merge_target->end);
            merge_target->start = 0;
            merge_target->end = 0;
            is_merged = _gf_true;
            merge_target = NULL;
        }

        /* try find */
        for (idx = 0; idx < inode->write_block_len; ++idx) {
            cur = p + idx;
            if (idx == merge_idx) {
                continue;
            }

            if (cur->start == 0 && cur->end == 0 ) {
                continue;
            }

            /*left*/
            if (merge_direct == 0
                    && merge_src->end >= cur->end
                    && merge_src->start <= cur->end ) {
                merge_target = cur;
                break;
            }

            /* right*/
            if (merge_direct == 1
                    && merge_src->start <= cur->start
                    && merge_src->end >= cur->start) {
                merge_target = cur;
                break;
            }
        }
    } while (merge_target);

    if (is_merged) {
        fsc_block_dump(this, inode, "merge end");
    }
    return 0;
}

int32_t
fsc_block_init(xlator_t *this, fsc_inode_t *inode)
{
    char * p = NULL;
    char * tmp = NULL;
    char * buff = NULL;
    char * attr = NULL;
    off_t start = 0;
    off_t end = 0;
    int32_t count = 0;
    off_t valid_cache_size = 0;
    int32_t idx = 0;
    int32_t ret = 0;
    fsc_block_t* cur = NULL;

    buff = GF_CALLOC(1, 512 * 1024, gf_fsc_mt_fsc_block_dump_t);

    ret = sys_fgetxattr(inode->fsc_fd, BLOCK_ATTR_NAME, buff, 512 * 1024);
    if (ret < 0) {
        gf_msg(this->name, GF_LOG_ERROR, errno, FS_CACHE_MSG_ERROR,
               "sys_fgetxattr error fd=%d,path=(%s)", inode->fsc_fd, inode->local_path);
    } else {
        attr = buff;
        gf_msg(this->name, GF_LOG_INFO, 0, FS_CACHE_MSG_INFO,
               "sys_fgetxattr success fd=%d,path=(%s),attr=%s", inode->fsc_fd, inode->local_path, attr);
    }

    if (attr) {
        for (p = attr; *p; p++) {
            if (*p == ' ') {
                count++;
            }
        }
    }

    inode->write_block_len = max(BLOCK_BASE_LEN, count);
    inode->write_block = GF_CALLOC(inode->write_block_len, sizeof(fsc_block_t), gf_fsc_mt_fsc_block_t);

    if (!attr)  {
        goto out;
    }
    tmp = attr;  /*like this: "0:24 45:100 " */
    for (p = attr; *p; p++) {
        if (*p == ':') {
            *p = 0;
            start = strtoull(tmp, NULL, 0);
            *p = ':';
            tmp = p + 1;
        }

        if (*p == ' ') {
            *p = 0;
            end = strtoull(tmp, NULL, 0);;
            *p = ' ';
            tmp = p + 1;

            cur = inode->write_block + idx;
            cur->start = start;
            cur->end = end;
            valid_cache_size += end - start;
            idx++;
        }
    }
    fsc_block_dump(this, inode, "init");
    inode->fsc_size = valid_cache_size;
out:
    return 0;
}

int32_t
fsc_block_remove(xlator_t *this, fsc_inode_t *inode, off_t offset, size_t size)
{
    int32_t idx = 0;
    fsc_block_t re_add = {
        0, 0
    };
    fsc_block_t* cur = NULL;
    fsc_block_t* p = inode->write_block;
    off_t this_end = offset + size;
    fsc_block_dump(this, inode, "remove start");

    for (idx = 0; idx < inode->write_block_len; ++idx) {
        cur = p + idx;
        if ( cur->start == 0 && cur->end == 0) {
            /* idle block*/
            continue;
        }

        if (offset == cur->start
                && this_end == cur->end) {
            cur->start = 0;
            cur->end = 0;
            goto out;
        }

        /*algin lef block*/
        if (offset == cur->start
                && this_end < cur->end) {
            cur->start = this_end;
            goto out;
        }

        /*algin right block*/
        if (offset > cur->start
                && this_end == cur->end) {
            cur->end = offset;
            goto out;
        }


        /*in block*/
        if (offset > cur->start
                && this_end < cur->end) {
            re_add.start = this_end;
            re_add.end = cur->end;

            cur->end = offset;
            goto out;
        }
    }
out:
    if (re_add.start != 0) {
        fsc_block_add(this, inode, re_add.start, re_add.end - re_add.start);
    }
    fsc_block_dump(this, inode, "remove end");
    return 0;
}

int32_t
fsc_block_add(xlator_t *this, fsc_inode_t *inode, off_t offset, size_t size)
{
    int32_t idx = 0;
    off_t this_end = offset + size;
    int32_t merge_direct = -1;
    int32_t merge_idx = -1;
    int32_t new_start = 0;

    fsc_block_t* cur = NULL;
    fsc_block_t* p = inode->write_block;

    fsc_block_dump(this, inode, "add start");
    for (idx = 0; idx < inode->write_block_len; ++idx) {
        cur = p + idx;
        if ( cur->start == 0 && cur->end == 0) {
            /* idle block*/
            continue;
        }

        /*in block*/
        if (offset >= cur->start
                && this_end <= cur->end) {
            goto out;
        }

        /*try left extend*/
        if (this_end >= cur->start
                && this_end <= cur->end) {

            if (cur->start > offset) {
                cur->start = offset;
                merge_idx = idx;
                merge_direct = 0;
                goto out;
            }
        }

        /*try right extend*/
        if (offset >= cur->start
                && offset <= cur->end) {
            if (cur->end < this_end) {
                cur->end = this_end;
                merge_idx = idx;
                merge_direct = 1;
                goto out;
            }
        }
    }

    for (idx = 0; idx < inode->write_block_len; ++idx) {
        cur = p + idx;
        if ( cur->start == 0 && cur->end == 0) {
            /* idle block*/
            cur->start = offset;
            cur->end = this_end;
            goto out;;
        }
    }

    /* not enough*/
    new_start = inode->write_block_len;
    inode->write_block_len += BLOCK_BASE_LEN;
    inode->write_block = GF_REALLOC(inode->write_block, inode->write_block_len * sizeof(fsc_block_t));
    if (!inode->write_block) {
        goto out;
    }
    gf_msg(this->name, GF_LOG_INFO, 0, FS_CACHE_MSG_INFO,
           "realloc block fsc=%p,path=(%s),oldlen=%d,newlen=%d", inode, inode->local_path, new_start, inode->write_block_len);
    p = inode->write_block;
    cur = p + new_start;
    cur->start = offset;
    cur->end = this_end;

    for (idx = new_start + 1; idx < inode->write_block_len; idx++) {
        cur = p + idx;
        cur->start = 0;
        cur->end = 0;
    }

out:
    fsc_block_dump(this, inode, "add end");
    if (merge_idx >= 0) {
        fsc_block_merge(this, inode, merge_idx, merge_direct);
    }

    return 0;
}

int32_t
fsc_block_is_cache(xlator_t *this, fsc_inode_t *inode, off_t offset, size_t size)
{
    int32_t idx = 0;
    size_t this_end = offset + size;
    fsc_block_t* cur = NULL;
    fsc_block_t* p = inode->write_block;
    for (idx = 0; idx < inode->write_block_len; ++idx) {
        cur = p + idx;
        if (offset >= cur->start
                && this_end <= cur->end) {
            return 0;
        }
    }
    /*fuse req last block,may be exceed the file realsize*/
    if (inode->ia_size > 0
            && offset + size > inode->ia_size
            && inode->fsc_size >= inode->ia_size) {
        return 0;
    }
    return -1;
}


int32_t
fsc_block_flush(xlator_t *this, fsc_inode_t *inode)
{
    char* buff = NULL;
    int32_t len = inode->write_block_len * 32;
    int32_t value_len = 0;
    buff = GF_CALLOC(1, len, gf_fsc_mt_fsc_block_dump_t);
    if (!buff) {
        return -1;
    }
    value_len = fsc_block_to_str(this, inode, buff, len);
    if (sys_fsetxattr(inode->fsc_fd, BLOCK_ATTR_NAME, buff, value_len, 0)) {
        gf_msg(this->name, GF_LOG_ERROR, errno, FS_CACHE_MSG_ERROR,
               "sys_fsetxattr error fd=%d,path=(%s)", inode->fsc_fd, inode->local_path);
    } else {
        gf_msg(this->name, GF_LOG_INFO, 0, FS_CACHE_MSG_INFO,
               "sys_fsetxattr success fd=%d,path=(%s),val=%s", inode->fsc_fd, inode->local_path, buff);
    }

    GF_FREE(buff);
    return 0;
}
