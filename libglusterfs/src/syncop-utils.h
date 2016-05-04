/*
  Copyright (c) 2015, Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _SYNCOP_UTILS_H
#define _SYNCOP_UTILS_H

typedef int (*syncop_dir_scan_fn_t) (xlator_t *subvol, gf_dirent_t *entry,
                                     loc_t *parent, void *data);
int
syncop_ftw (xlator_t *subvol, loc_t *loc, int pid, void *data,
            int (*fn) (xlator_t *subvol, gf_dirent_t *entry, loc_t *parent,
                       void *data));

int
syncop_mt_dir_scan (call_frame_t *frame, xlator_t *subvol, loc_t *loc, int pid,
                    void *data, syncop_dir_scan_fn_t fn, dict_t *xdata,
                    uint32_t max_jobs, uint32_t max_qlen);

int
syncop_dir_scan (xlator_t *subvol, loc_t *loc, int pid, void *data,
                 int (*fn) (xlator_t *subvol, gf_dirent_t *entry, loc_t *parent,
                            void *data));

int
syncop_dirfd (xlator_t *subvol, loc_t *loc, fd_t **fd, int pid);

int
syncop_is_subvol_local (xlator_t *this, loc_t *loc, gf_boolean_t *is_local);

int
syncop_gfid_to_path (inode_table_t *itable, xlator_t *subvol, uuid_t gfid,
                     char **path_p);

int
syncop_ftw_throttle (xlator_t *subvol, loc_t *loc, int pid, void *data,
                     int (*fn) (xlator_t *subvol, gf_dirent_t *entry,
                                loc_t *parent, void *data),
                     int count, int sleep_time);
#endif /* _SYNCOP_H */
