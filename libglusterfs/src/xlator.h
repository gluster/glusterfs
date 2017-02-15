/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _XLATOR_H
#define _XLATOR_H

#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>

#include "event-history.h"
#include "logging.h"
#include "common-utils.h"
#include "dict.h"
#include "compat.h"
#include "list.h"
#include "latency.h"
#include "compat-uuid.h"

#define FIRST_CHILD(xl) (xl->children->xlator)
#define SECOND_CHILD(xl) (xl->children->next->xlator)

#define GF_SET_ATTR_MODE  0x1
#define GF_SET_ATTR_UID   0x2
#define GF_SET_ATTR_GID   0x4
#define GF_SET_ATTR_SIZE  0x8
#define GF_SET_ATTR_ATIME 0x10
#define GF_SET_ATTR_MTIME 0x20

#define gf_attr_mode_set(mode)  ((mode) & GF_SET_ATTR_MODE)
#define gf_attr_uid_set(mode)   ((mode) & GF_SET_ATTR_UID)
#define gf_attr_gid_set(mode)   ((mode) & GF_SET_ATTR_GID)
#define gf_attr_size_set(mode)  ((mode) & GF_SET_ATTR_SIZE)
#define gf_attr_atime_set(mode) ((mode) & GF_SET_ATTR_ATIME)
#define gf_attr_mtime_set(mode) ((mode) & GF_SET_ATTR_MTIME)

struct _xlator;
typedef struct _xlator xlator_t;
struct _dir_entry_t;
typedef struct _dir_entry_t dir_entry_t;
struct _gf_dirent_t;
typedef struct _gf_dirent_t gf_dirent_t;
struct _loc;
typedef struct _loc loc_t;


typedef int32_t (*event_notify_fn_t) (xlator_t *this, int32_t event, void *data,
                                      ...);

#include "list.h"
#include "gf-dirent.h"
#include "stack.h"
#include "iobuf.h"
#include "inode.h"
#include "fd.h"
#include "globals.h"
#include "iatt.h"
#include "options.h"
#include "client_t.h"


struct _loc {
        const char *path;
        const char *name;
        inode_t    *inode;
        inode_t    *parent;
        /* Currently all location based operations are through 'gfid' of inode.
         * But the 'inode->gfid' only gets set in higher most layer (as in,
         * 'fuse', 'protocol/server', or 'nfs/server'). So if translators want
         * to send fops on a inode before the 'inode->gfid' is set, they have to
         * make use of below 'gfid' fields
         */
        uuid_t      gfid;
        uuid_t      pargfid;
};


typedef int32_t (*fop_getspec_cbk_t) (call_frame_t *frame,
                                      void *cookie,
                                      xlator_t *this,
                                      int32_t op_ret,
                                      int32_t op_errno,
                                      char *spec_data);

typedef int32_t (*fop_rchecksum_cbk_t) (call_frame_t *frame,
                                        void *cookie,
                                        xlator_t *this,
                                        int32_t op_ret,
                                        int32_t op_errno,
                                        uint32_t weak_checksum,
                                        uint8_t *strong_checksum,
                                        dict_t *xdata);


typedef int32_t (*fop_getspec_t) (call_frame_t *frame,
                                  xlator_t *this,
                                  const char *key,
                                  int32_t flag);

typedef int32_t (*fop_rchecksum_t) (call_frame_t *frame,
                                    xlator_t *this,
                                    fd_t *fd, off_t offset,
                                    int32_t len, dict_t *xdata);


typedef int32_t (*fop_lookup_cbk_t) (call_frame_t *frame,
                                     void *cookie,
                                     xlator_t *this,
                                     int32_t op_ret,
                                     int32_t op_errno,
                                     inode_t *inode,
                                     struct iatt *buf,
                                     dict_t *xdata,
                                     struct iatt *postparent);

typedef int32_t (*fop_stat_cbk_t) (call_frame_t *frame,
                                   void *cookie,
                                   xlator_t *this,
                                   int32_t op_ret,
                                   int32_t op_errno,
                                   struct iatt *buf, dict_t *xdata);

typedef int32_t (*fop_fstat_cbk_t) (call_frame_t *frame,
                                    void *cookie,
                                    xlator_t *this,
                                    int32_t op_ret,
                                    int32_t op_errno,
                                    struct iatt *buf, dict_t *xdata);

typedef int32_t (*fop_truncate_cbk_t) (call_frame_t *frame,
                                       void *cookie,
                                       xlator_t *this,
                                       int32_t op_ret,
                                       int32_t op_errno,
                                       struct iatt *prebuf,
                                       struct iatt *postbuf, dict_t *xdata);

typedef int32_t (*fop_ftruncate_cbk_t) (call_frame_t *frame,
                                        void *cookie,
                                        xlator_t *this,
                                        int32_t op_ret,
                                        int32_t op_errno,
                                        struct iatt *prebuf,
                                        struct iatt *postbuf, dict_t *xdata);

typedef int32_t (*fop_access_cbk_t) (call_frame_t *frame,
                                     void *cookie,
                                     xlator_t *this,
                                     int32_t op_ret,
                                     int32_t op_errno, dict_t *xdata);

typedef int32_t (*fop_readlink_cbk_t) (call_frame_t *frame,
                                       void *cookie,
                                       xlator_t *this,
                                       int32_t op_ret,
                                       int32_t op_errno,
                                       const char *path,
                                       struct iatt *buf, dict_t *xdata);

typedef int32_t (*fop_mknod_cbk_t) (call_frame_t *frame,
                                    void *cookie,
                                    xlator_t *this,
                                    int32_t op_ret,
                                    int32_t op_errno,
                                    inode_t *inode,
                                    struct iatt *buf,
                                    struct iatt *preparent,
                                    struct iatt *postparent, dict_t *xdata);

typedef int32_t (*fop_mkdir_cbk_t) (call_frame_t *frame,
                                    void *cookie,
                                    xlator_t *this,
                                    int32_t op_ret,
                                    int32_t op_errno,
                                    inode_t *inode,
                                    struct iatt *buf,
                                    struct iatt *preparent,
                                    struct iatt *postparent, dict_t *xdata);

typedef int32_t (*fop_unlink_cbk_t) (call_frame_t *frame,
                                     void *cookie,
                                     xlator_t *this,
                                     int32_t op_ret,
                                     int32_t op_errno,
                                     struct iatt *preparent,
                                     struct iatt *postparent, dict_t *xdata);

typedef int32_t (*fop_rmdir_cbk_t) (call_frame_t *frame,
                                    void *cookie,
                                    xlator_t *this,
                                    int32_t op_ret,
                                    int32_t op_errno,
                                    struct iatt *preparent,
                                    struct iatt *postparent, dict_t *xdata);

typedef int32_t (*fop_symlink_cbk_t) (call_frame_t *frame,
                                      void *cookie,
                                      xlator_t *this,
                                      int32_t op_ret,
                                      int32_t op_errno,
                                      inode_t *inode,
                                      struct iatt *buf,
                                      struct iatt *preparent,
                                      struct iatt *postparent, dict_t *xdata);

typedef int32_t (*fop_rename_cbk_t) (call_frame_t *frame,
                                     void *cookie,
                                     xlator_t *this,
                                     int32_t op_ret,
                                     int32_t op_errno,
                                     struct iatt *buf,
                                     struct iatt *preoldparent,
                                     struct iatt *postoldparent,
                                     struct iatt *prenewparent,
                                     struct iatt *postnewparent, dict_t *xdata);

typedef int32_t (*fop_link_cbk_t) (call_frame_t *frame,
                                   void *cookie,
                                   xlator_t *this,
                                   int32_t op_ret,
                                   int32_t op_errno,
                                   inode_t *inode,
                                   struct iatt *buf,
                                   struct iatt *preparent,
                                   struct iatt *postparent, dict_t *xdata);

typedef int32_t (*fop_create_cbk_t) (call_frame_t *frame,
                                     void *cookie,
                                     xlator_t *this,
                                     int32_t op_ret,
                                     int32_t op_errno,
                                     fd_t *fd,
                                     inode_t *inode,
                                     struct iatt *buf,
                                     struct iatt *preparent,
                                     struct iatt *postparent, dict_t *xdata);

typedef int32_t (*fop_open_cbk_t) (call_frame_t *frame,
                                   void *cookie,
                                   xlator_t *this,
                                   int32_t op_ret,
                                   int32_t op_errno,
                                   fd_t *fd, dict_t *xdata);

typedef int32_t (*fop_readv_cbk_t) (call_frame_t *frame,
                                    void *cookie,
                                    xlator_t *this,
                                    int32_t op_ret,
                                    int32_t op_errno,
                                    struct iovec *vector,
                                    int32_t count,
                                    struct iatt *stbuf,
                                    struct iobref *iobref, dict_t *xdata);

typedef int32_t (*fop_writev_cbk_t) (call_frame_t *frame,
                                     void *cookie,
                                     xlator_t *this,
                                     int32_t op_ret,
                                     int32_t op_errno,
                                     struct iatt *prebuf,
                                     struct iatt *postbuf, dict_t *xdata);

typedef int32_t (*fop_flush_cbk_t) (call_frame_t *frame,
                                    void *cookie,
                                    xlator_t *this,
                                    int32_t op_ret,
                                    int32_t op_errno, dict_t *xdata);

typedef int32_t (*fop_fsync_cbk_t) (call_frame_t *frame,
                                    void *cookie,
                                    xlator_t *this,
                                    int32_t op_ret,
                                    int32_t op_errno,
                                    struct iatt *prebuf,
                                    struct iatt *postbuf, dict_t *xdata);

typedef int32_t (*fop_opendir_cbk_t) (call_frame_t *frame,
                                      void *cookie,
                                      xlator_t *this,
                                      int32_t op_ret,
                                      int32_t op_errno,
                                      fd_t *fd, dict_t *xdata);

typedef int32_t (*fop_fsyncdir_cbk_t) (call_frame_t *frame,
                                       void *cookie,
                                       xlator_t *this,
                                       int32_t op_ret,
                                       int32_t op_errno, dict_t *xdata);

typedef int32_t (*fop_statfs_cbk_t) (call_frame_t *frame,
                                     void *cookie,
                                     xlator_t *this,
                                     int32_t op_ret,
                                     int32_t op_errno,
                                     struct statvfs *buf, dict_t *xdata);

typedef int32_t (*fop_setxattr_cbk_t) (call_frame_t *frame,
                                       void *cookie,
                                       xlator_t *this,
                                       int32_t op_ret,
                                       int32_t op_errno, dict_t *xdata);

typedef int32_t (*fop_getxattr_cbk_t) (call_frame_t *frame,
                                       void *cookie,
                                       xlator_t *this,
                                       int32_t op_ret,
                                       int32_t op_errno,
                                       dict_t *dict, dict_t *xdata);

typedef int32_t (*fop_fsetxattr_cbk_t) (call_frame_t *frame,
                                        void *cookie,
                                        xlator_t *this,
                                        int32_t op_ret,
                                        int32_t op_errno, dict_t *xdata);

typedef int32_t (*fop_fgetxattr_cbk_t) (call_frame_t *frame,
                                        void *cookie,
                                        xlator_t *this,
                                        int32_t op_ret,
                                        int32_t op_errno,
                                        dict_t *dict, dict_t *xdata);

typedef int32_t (*fop_removexattr_cbk_t) (call_frame_t *frame,
                                          void *cookie,
                                          xlator_t *this,
                                          int32_t op_ret,
                                          int32_t op_errno, dict_t *xdata);

typedef int32_t (*fop_fremovexattr_cbk_t) (call_frame_t *frame,
                                           void *cookie,
                                           xlator_t *this,
                                           int32_t op_ret,
                                           int32_t op_errno, dict_t *xdata);

typedef int32_t (*fop_lk_cbk_t) (call_frame_t *frame,
                                 void *cookie,
                                 xlator_t *this,
                                 int32_t op_ret,
                                 int32_t op_errno,
                                 struct gf_flock *flock, dict_t *xdata);

typedef int32_t (*fop_inodelk_cbk_t) (call_frame_t *frame,
                                      void *cookie,
                                      xlator_t *this,
                                      int32_t op_ret,
                                      int32_t op_errno, dict_t *xdata);

typedef int32_t (*fop_finodelk_cbk_t) (call_frame_t *frame,
                                       void *cookie,
                                       xlator_t *this,
                                       int32_t op_ret,
                                       int32_t op_errno, dict_t *xdata);

typedef int32_t (*fop_entrylk_cbk_t) (call_frame_t *frame,
                                      void *cookie,
                                      xlator_t *this,
                                      int32_t op_ret,
                                      int32_t op_errno, dict_t *xdata);

typedef int32_t (*fop_fentrylk_cbk_t) (call_frame_t *frame,
                                       void *cookie,
                                       xlator_t *this,
                                       int32_t op_ret,
                                       int32_t op_errno, dict_t *xdata);

typedef int32_t (*fop_readdir_cbk_t) (call_frame_t *frame,
                                      void *cookie,
                                      xlator_t *this,
                                      int32_t op_ret,
                                      int32_t op_errno,
                                      gf_dirent_t *entries, dict_t *xdata);

typedef int32_t (*fop_readdirp_cbk_t) (call_frame_t *frame,
                                       void *cookie,
                                       xlator_t *this,
                                       int32_t op_ret,
                                       int32_t op_errno,
                                       gf_dirent_t *entries, dict_t *xdata);

typedef int32_t (*fop_xattrop_cbk_t) (call_frame_t *frame,
                                      void *cookie,
                                      xlator_t *this,
                                      int32_t op_ret,
                                      int32_t op_errno,
                                      dict_t *xattr, dict_t *xdata);

typedef int32_t (*fop_fxattrop_cbk_t) (call_frame_t *frame,
                                       void *cookie,
                                       xlator_t *this,
                                       int32_t op_ret,
                                       int32_t op_errno,
                                       dict_t *xattr, dict_t *xdata);


typedef int32_t (*fop_setattr_cbk_t) (call_frame_t *frame,
                                      void *cookie,
                                      xlator_t *this,
                                      int32_t op_ret,
                                      int32_t op_errno,
                                      struct iatt *preop_stbuf,
                                      struct iatt *postop_stbuf, dict_t *xdata);

typedef int32_t (*fop_fsetattr_cbk_t) (call_frame_t *frame,
                                       void *cookie,
                                       xlator_t *this,
                                       int32_t op_ret,
                                       int32_t op_errno,
                                       struct iatt *preop_stbuf,
                                       struct iatt *postop_stbuf, dict_t *xdata);

typedef int32_t (*fop_fallocate_cbk_t) (call_frame_t *frame,
                                        void *cookie,
                                        xlator_t *this,
                                        int32_t op_ret,
                                        int32_t op_errno,
                                        struct iatt *preop_stbuf,
                                        struct iatt *postop_stbuf, dict_t *xdata);

typedef int32_t (*fop_discard_cbk_t) (call_frame_t *frame,
                                      void *cookie,
                                      xlator_t *this,
                                      int32_t op_ret,
                                      int32_t op_errno,
                                      struct iatt *preop_stbuf,
                                      struct iatt *postop_stbuf, dict_t *xdata);

typedef int32_t (*fop_zerofill_cbk_t) (call_frame_t *frame,
                                      void *cookie,
                                      xlator_t *this,
                                      int32_t op_ret,
                                      int32_t op_errno,
                                      struct iatt *preop_stbuf,
                                      struct iatt *postop_stbuf, dict_t *xdata);

typedef int32_t (*fop_ipc_cbk_t) (call_frame_t *frame, void *cookie,
                                 xlator_t *this, int32_t op_ret,
                                 int32_t op_errno, dict_t *xdata);

typedef int32_t (*fop_seek_cbk_t) (call_frame_t *frame, void *cookie,
                                   xlator_t *this, int32_t op_ret,
                                   int32_t op_errno, off_t offset,
                                   dict_t *xdata);

typedef int32_t (*fop_lease_cbk_t) (call_frame_t *frame, void *cookie,
                                    xlator_t *this, int32_t op_ret,
                                    int32_t op_errno, struct gf_lease *lease,
                                    dict_t *xdata);
typedef int32_t (*fop_compound_cbk_t) (call_frame_t *frame,
                                       void *cookie, xlator_t *this,
                                       int32_t op_ret, int32_t op_errno,
                                       void *data, dict_t *xdata);

typedef int32_t (*fop_getactivelk_cbk_t) (call_frame_t *frame, void *cookie,
                                           xlator_t *this, int32_t op_ret,
                                           int32_t op_errno,
                                           lock_migration_info_t *locklist,
                                           dict_t *xdata);

typedef int32_t (*fop_setactivelk_cbk_t) (call_frame_t *frame, void *cookie,
                                            xlator_t *this, int32_t op_ret,
                                            int32_t op_errno, dict_t *xdata);

typedef int32_t (*fop_lookup_t) (call_frame_t *frame,
                                 xlator_t *this,
                                 loc_t *loc,
                                 dict_t *xdata);

typedef int32_t (*fop_stat_t) (call_frame_t *frame,
                               xlator_t *this,
                               loc_t *loc, dict_t *xdata);

typedef int32_t (*fop_fstat_t) (call_frame_t *frame,
                                xlator_t *this,
                                fd_t *fd, dict_t *xdata);

typedef int32_t (*fop_truncate_t) (call_frame_t *frame,
                                   xlator_t *this,
                                   loc_t *loc,
                                   off_t offset, dict_t *xdata);

typedef int32_t (*fop_ftruncate_t) (call_frame_t *frame,
                                    xlator_t *this,
                                    fd_t *fd,
                                    off_t offset, dict_t *xdata);

typedef int32_t (*fop_access_t) (call_frame_t *frame,
                                 xlator_t *this,
                                 loc_t *loc,
                                 int32_t mask, dict_t *xdata);

typedef int32_t (*fop_readlink_t) (call_frame_t *frame,
                                   xlator_t *this,
                                   loc_t *loc,
                                   size_t size, dict_t *xdata);

typedef int32_t (*fop_mknod_t) (call_frame_t *frame, xlator_t *this,
                                loc_t *loc, mode_t mode, dev_t rdev,
                                mode_t umask, dict_t *xdata);

typedef int32_t (*fop_mkdir_t) (call_frame_t *frame, xlator_t *this, loc_t *loc,
                                mode_t mode, mode_t umask, dict_t *xdata);

typedef int32_t (*fop_unlink_t) (call_frame_t *frame, xlator_t *this,
                                 loc_t *loc, int xflags, dict_t *xdata);

typedef int32_t (*fop_rmdir_t) (call_frame_t *frame, xlator_t *this,
                                loc_t *loc, int xflags, dict_t *xdata);

typedef int32_t (*fop_symlink_t) (call_frame_t *frame, xlator_t *this,
                                  const char *linkname, loc_t *loc,
                                  mode_t umask, dict_t *xdata);

typedef int32_t (*fop_rename_t) (call_frame_t *frame,
                                 xlator_t *this,
                                 loc_t *oldloc,
                                 loc_t *newloc, dict_t *xdata);

typedef int32_t (*fop_link_t) (call_frame_t *frame,
                               xlator_t *this,
                               loc_t *oldloc,
                               loc_t *newloc, dict_t *xdata);

typedef int32_t (*fop_create_t) (call_frame_t *frame, xlator_t *this,
                                 loc_t *loc, int32_t flags, mode_t mode,
                                 mode_t umask, fd_t *fd, dict_t *xdata);

/* Tell subsequent writes on the fd_t to fsync after every writev fop without
 * requiring a fsync fop.
 */
#define GF_OPEN_FSYNC   0x01

/* Tell write-behind to disable writing behind despite O_SYNC not being set.
 */
#define GF_OPEN_NOWB    0x02

typedef int32_t (*fop_open_t) (call_frame_t *frame, xlator_t *this,
                               loc_t *loc, int32_t flags,
                               fd_t *fd, dict_t *xdata);

typedef int32_t (*fop_readv_t) (call_frame_t *frame,
                                xlator_t *this,
                                fd_t *fd,
                                size_t size,
                                off_t offset,
                                uint32_t flags, dict_t *xdata);

typedef int32_t (*fop_writev_t) (call_frame_t *frame,
                                 xlator_t *this,
                                 fd_t *fd,
                                 struct iovec *vector,
                                 int32_t count,
                                 off_t offset,
                                 uint32_t flags,
                                 struct iobref *iobref, dict_t *xdata);

typedef int32_t (*fop_flush_t) (call_frame_t *frame,
                                xlator_t *this,
                                fd_t *fd, dict_t *xdata);

typedef int32_t (*fop_fsync_t) (call_frame_t *frame,
                                xlator_t *this,
                                fd_t *fd,
                                int32_t datasync, dict_t *xdata);

typedef int32_t (*fop_opendir_t) (call_frame_t *frame,
                                  xlator_t *this,
                                  loc_t *loc,
                                  fd_t *fd, dict_t *xdata);

typedef int32_t (*fop_fsyncdir_t) (call_frame_t *frame,
                                   xlator_t *this,
                                   fd_t *fd,
                                   int32_t datasync, dict_t *xdata);

typedef int32_t (*fop_statfs_t) (call_frame_t *frame,
                                 xlator_t *this,
                                 loc_t *loc, dict_t *xdata);

typedef int32_t (*fop_setxattr_t) (call_frame_t *frame,
                                   xlator_t *this,
                                   loc_t *loc,
                                   dict_t *dict,
                                   int32_t flags, dict_t *xdata);

typedef int32_t (*fop_getxattr_t) (call_frame_t *frame,
                                   xlator_t *this,
                                   loc_t *loc,
                                   const char *name, dict_t *xdata);

typedef int32_t (*fop_fsetxattr_t) (call_frame_t *frame,
                                    xlator_t *this,
                                    fd_t *fd,
                                    dict_t *dict,
                                    int32_t flags, dict_t *xdata);

typedef int32_t (*fop_fgetxattr_t) (call_frame_t *frame,
                                    xlator_t *this,
                                    fd_t *fd,
                                    const char *name, dict_t *xdata);

typedef int32_t (*fop_removexattr_t) (call_frame_t *frame,
                                      xlator_t *this,
                                      loc_t *loc,
                                      const char *name, dict_t *xdata);

typedef int32_t (*fop_fremovexattr_t) (call_frame_t *frame,
                                       xlator_t *this,
                                       fd_t *fd,
                                       const char *name, dict_t *xdata);

typedef int32_t (*fop_lk_t) (call_frame_t *frame,
                             xlator_t *this,
                             fd_t *fd,
                             int32_t cmd,
                             struct gf_flock *flock, dict_t *xdata);

typedef int32_t (*fop_inodelk_t) (call_frame_t *frame,
                                  xlator_t *this,
                                  const char *volume,
                                  loc_t *loc,
                                  int32_t cmd,
                                  struct gf_flock *flock, dict_t *xdata);

typedef int32_t (*fop_finodelk_t) (call_frame_t *frame,
                                   xlator_t *this,
                                   const char *volume,
                                   fd_t *fd,
                                   int32_t cmd,
                                   struct gf_flock *flock, dict_t *xdata);

typedef int32_t (*fop_entrylk_t) (call_frame_t *frame,
                                  xlator_t *this,
                                  const char *volume, loc_t *loc,
                                  const char *basename, entrylk_cmd cmd,
                                  entrylk_type type, dict_t *xdata);

typedef int32_t (*fop_fentrylk_t) (call_frame_t *frame,
                                   xlator_t *this,
                                   const char *volume, fd_t *fd,
                                   const char *basename, entrylk_cmd cmd,
                                   entrylk_type type, dict_t *xdata);

typedef int32_t (*fop_readdir_t) (call_frame_t *frame,
                                  xlator_t *this,
                                  fd_t *fd,
                                  size_t size,
                                  off_t offset, dict_t *xdata);

typedef int32_t (*fop_readdirp_t) (call_frame_t *frame,
                                   xlator_t *this,
                                   fd_t *fd,
                                   size_t size,
                                   off_t offset,
                                   dict_t *xdata);

typedef int32_t (*fop_xattrop_t) (call_frame_t *frame,
                                  xlator_t *this,
                                  loc_t *loc,
                                  gf_xattrop_flags_t optype,
                                  dict_t *xattr, dict_t *xdata);

typedef int32_t (*fop_fxattrop_t) (call_frame_t *frame,
                                   xlator_t *this,
                                   fd_t *fd,
                                   gf_xattrop_flags_t optype,
                                   dict_t *xattr, dict_t *xdata);

typedef int32_t (*fop_setattr_t) (call_frame_t *frame,
                                  xlator_t *this,
                                  loc_t *loc,
                                  struct iatt *stbuf,
                                  int32_t valid, dict_t *xdata);

typedef int32_t (*fop_fsetattr_t) (call_frame_t *frame,
                                   xlator_t *this,
                                   fd_t *fd,
                                   struct iatt *stbuf,
                                   int32_t valid, dict_t *xdata);

typedef int32_t (*fop_fallocate_t) (call_frame_t *frame,
                                    xlator_t *this,
                                    fd_t *fd,
				    int32_t keep_size,
				    off_t offset,
				    size_t len,
                                    dict_t *xdata);

typedef int32_t (*fop_discard_t) (call_frame_t *frame,
                                  xlator_t *this,
                                  fd_t *fd,
				  off_t offset,
				  size_t len,
                                  dict_t *xdata);

typedef int32_t (*fop_zerofill_t) (call_frame_t *frame,
                                  xlator_t *this,
                                  fd_t *fd,
                                  off_t offset,
                                  off_t len,
                                  dict_t *xdata);

typedef int32_t (*fop_ipc_t) (call_frame_t *frame, xlator_t *this, int32_t op,
                              dict_t *xdata);

typedef int32_t (*fop_seek_t) (call_frame_t *frame, xlator_t *this, fd_t *fd,
                               off_t offset, gf_seek_what_t what,
                               dict_t *xdata);

typedef int32_t (*fop_lease_t) (call_frame_t *frame, xlator_t *this, loc_t *loc,
                                struct gf_lease *lease, dict_t *xdata);

typedef int32_t (*fop_compound_t) (call_frame_t *frame, xlator_t *this,
                                   void *args, dict_t *xdata);

typedef int32_t (*fop_getactivelk_t) (call_frame_t *frame, xlator_t *this,
                                       loc_t *loc, dict_t *xdata);

typedef int32_t (*fop_setactivelk_t) (call_frame_t *frame, xlator_t *this,
                                      loc_t *loc,
                                      lock_migration_info_t *locklist,
                                      dict_t *xdata);

struct xlator_fops {
        fop_lookup_t         lookup;
        fop_stat_t           stat;
        fop_fstat_t          fstat;
        fop_truncate_t       truncate;
        fop_ftruncate_t      ftruncate;
        fop_access_t         access;
        fop_readlink_t       readlink;
        fop_mknod_t          mknod;
        fop_mkdir_t          mkdir;
        fop_unlink_t         unlink;
        fop_rmdir_t          rmdir;
        fop_symlink_t        symlink;
        fop_rename_t         rename;
        fop_link_t           link;
        fop_create_t         create;
        fop_open_t           open;
        fop_readv_t          readv;
        fop_writev_t         writev;
        fop_flush_t          flush;
        fop_fsync_t          fsync;
        fop_opendir_t        opendir;
        fop_readdir_t        readdir;
        fop_readdirp_t       readdirp;
        fop_fsyncdir_t       fsyncdir;
        fop_statfs_t         statfs;
        fop_setxattr_t       setxattr;
        fop_getxattr_t       getxattr;
        fop_fsetxattr_t      fsetxattr;
        fop_fgetxattr_t      fgetxattr;
        fop_removexattr_t    removexattr;
        fop_fremovexattr_t   fremovexattr;
        fop_lk_t             lk;
        fop_inodelk_t        inodelk;
        fop_finodelk_t       finodelk;
        fop_entrylk_t        entrylk;
        fop_fentrylk_t       fentrylk;
        fop_rchecksum_t      rchecksum;
        fop_xattrop_t        xattrop;
        fop_fxattrop_t       fxattrop;
        fop_setattr_t        setattr;
        fop_fsetattr_t       fsetattr;
        fop_getspec_t        getspec;
	fop_fallocate_t	     fallocate;
	fop_discard_t	     discard;
        fop_zerofill_t       zerofill;
        fop_ipc_t            ipc;
        fop_seek_t           seek;
        fop_lease_t          lease;
        fop_compound_t       compound;
        fop_getactivelk_t   getactivelk;
        fop_setactivelk_t  setactivelk;

        /* these entries are used for a typechecking hack in STACK_WIND _only_ */
        fop_lookup_cbk_t         lookup_cbk;
        fop_stat_cbk_t           stat_cbk;
        fop_fstat_cbk_t          fstat_cbk;
        fop_truncate_cbk_t       truncate_cbk;
        fop_ftruncate_cbk_t      ftruncate_cbk;
        fop_access_cbk_t         access_cbk;
        fop_readlink_cbk_t       readlink_cbk;
        fop_mknod_cbk_t          mknod_cbk;
        fop_mkdir_cbk_t          mkdir_cbk;
        fop_unlink_cbk_t         unlink_cbk;
        fop_rmdir_cbk_t          rmdir_cbk;
        fop_symlink_cbk_t        symlink_cbk;
        fop_rename_cbk_t         rename_cbk;
        fop_link_cbk_t           link_cbk;
        fop_create_cbk_t         create_cbk;
        fop_open_cbk_t           open_cbk;
        fop_readv_cbk_t          readv_cbk;
        fop_writev_cbk_t         writev_cbk;
        fop_flush_cbk_t          flush_cbk;
        fop_fsync_cbk_t          fsync_cbk;
        fop_opendir_cbk_t        opendir_cbk;
        fop_readdir_cbk_t        readdir_cbk;
        fop_readdirp_cbk_t       readdirp_cbk;
        fop_fsyncdir_cbk_t       fsyncdir_cbk;
        fop_statfs_cbk_t         statfs_cbk;
        fop_setxattr_cbk_t       setxattr_cbk;
        fop_getxattr_cbk_t       getxattr_cbk;
        fop_fsetxattr_cbk_t      fsetxattr_cbk;
        fop_fgetxattr_cbk_t      fgetxattr_cbk;
        fop_removexattr_cbk_t    removexattr_cbk;
        fop_fremovexattr_cbk_t   fremovexattr_cbk;
        fop_lk_cbk_t             lk_cbk;
        fop_inodelk_cbk_t        inodelk_cbk;
        fop_finodelk_cbk_t       finodelk_cbk;
        fop_entrylk_cbk_t        entrylk_cbk;
        fop_fentrylk_cbk_t       fentrylk_cbk;
        fop_rchecksum_cbk_t      rchecksum_cbk;
        fop_xattrop_cbk_t        xattrop_cbk;
        fop_fxattrop_cbk_t       fxattrop_cbk;
        fop_setattr_cbk_t        setattr_cbk;
        fop_fsetattr_cbk_t       fsetattr_cbk;
        fop_getspec_cbk_t        getspec_cbk;
	fop_fallocate_cbk_t	 fallocate_cbk;
	fop_discard_cbk_t	 discard_cbk;
        fop_zerofill_cbk_t       zerofill_cbk;
        fop_ipc_cbk_t            ipc_cbk;
        fop_seek_cbk_t           seek_cbk;
        fop_lease_cbk_t          lease_cbk;
        fop_compound_cbk_t       compound_cbk;
        fop_getactivelk_cbk_t   getactivelk_cbk;
        fop_setactivelk_cbk_t  setactivelk_cbk;
};

typedef int32_t (*cbk_forget_t) (xlator_t *this,
                                 inode_t *inode);

typedef int32_t (*cbk_release_t) (xlator_t *this,
                                  fd_t *fd);

typedef int32_t (*cbk_invalidate_t)(xlator_t *this, inode_t *inode);

typedef int32_t (*cbk_client_t)(xlator_t *this, client_t *client);

typedef void (*cbk_ictxmerge_t) (xlator_t *this, fd_t *fd,
                                 inode_t *inode, inode_t *linked_inode);

typedef size_t (*cbk_inodectx_size_t)(xlator_t *this, inode_t *inode);

typedef size_t (*cbk_fdctx_size_t)(xlator_t *this, fd_t *fd);

struct xlator_cbks {
        cbk_forget_t             forget;
        cbk_release_t            release;
        cbk_release_t            releasedir;
	cbk_invalidate_t         invalidate;
        cbk_client_t             client_destroy;
        cbk_client_t             client_disconnect;
        cbk_ictxmerge_t          ictxmerge;
        cbk_inodectx_size_t      ictxsize;
        cbk_fdctx_size_t         fdctxsize;
};

typedef int32_t (*dumpop_priv_t) (xlator_t *this);

typedef int32_t (*dumpop_inode_t) (xlator_t *this);

typedef int32_t (*dumpop_fd_t)  (xlator_t   *this);

typedef int32_t (*dumpop_inodectx_t) (xlator_t *this, inode_t *ino);

typedef int32_t (*dumpop_fdctx_t) (xlator_t *this, fd_t *fd);

typedef int32_t (*dumpop_priv_to_dict_t) (xlator_t *this, dict_t *dict);

typedef int32_t (*dumpop_inode_to_dict_t) (xlator_t *this, dict_t *dict);

typedef int32_t (*dumpop_fd_to_dict_t) (xlator_t *this, dict_t *dict);

typedef int32_t (*dumpop_inodectx_to_dict_t) (xlator_t *this, inode_t *ino,
                                              dict_t *dict);

typedef int32_t (*dumpop_fdctx_to_dict_t) (xlator_t *this, fd_t *fd,
                                           dict_t *dict);

typedef int32_t (*dumpop_eh_t) (xlator_t *this);

struct xlator_dumpops {
        dumpop_priv_t                   priv;
        dumpop_inode_t                  inode;
        dumpop_fd_t                     fd;
        dumpop_inodectx_t               inodectx;
        dumpop_fdctx_t                  fdctx;
        dumpop_priv_to_dict_t           priv_to_dict;
        dumpop_inode_to_dict_t          inode_to_dict;
        dumpop_fd_to_dict_t             fd_to_dict;
        dumpop_inodectx_to_dict_t       inodectx_to_dict;
        dumpop_fdctx_to_dict_t          fdctx_to_dict;
        dumpop_eh_t                     history;
};

typedef struct xlator_list {
        xlator_t           *xlator;
        struct xlator_list *next;
} xlator_list_t;


struct _xlator {
        /* Built during parsing */
        char          *name;
        char          *type;
        char          *instance_name;  /* Used for multi NFSd */
        xlator_t      *next;
        xlator_t      *prev;
        xlator_list_t *parents;
        xlator_list_t *children;
        dict_t        *options;

        /* Set after doing dlopen() */
        void                  *dlhandle;
        struct xlator_fops    *fops;
        struct xlator_cbks    *cbks;
        struct xlator_dumpops *dumpops;
        struct list_head       volume_options;  /* list of volume_option_t */

        void              (*fini) (xlator_t *this);
        int32_t           (*init) (xlator_t *this);
        int32_t           (*reconfigure) (xlator_t *this, dict_t *options);
	int32_t           (*mem_acct_init) (xlator_t *this);
	event_notify_fn_t notify;

        gf_loglevel_t    loglevel;   /* Log level for translator */

        /* for latency measurement */
        fop_latency_t latencies[GF_FOP_MAXVALUE];

        /* Misc */
        eh_t               *history; /* event history context */
        glusterfs_ctx_t    *ctx;
        glusterfs_graph_t  *graph; /* not set for fuse */
        inode_table_t      *itable;
        char                init_succeeded;
        void               *private;
        struct mem_acct    *mem_acct;
        uint64_t            winds;
        char                switched;

        /* for the memory pool of 'frame->local' */
        struct mem_pool    *local_pool;
        gf_boolean_t        is_autoloaded;

        /* Saved volfile ID (used for multiplexing) */
        char               *volfile_id;

        /* Its used as an index to inode_ctx*/
        uint32_t            xl_id;
};

typedef struct {
        int32_t                 (*init) (xlator_t *this);
        void                    (*fini) (xlator_t *this);
        int32_t                 (*reconfigure) (xlator_t *this,
                                                dict_t *options);
        event_notify_fn_t       notify;
} class_methods_t;

#define xlator_has_parent(xl) (xl->parents != NULL)

#define XLATOR_NOTIFY(_xl, params ...)          \
        do {                                    \
                xlator_t *_old_THIS = NULL;     \
                                                \
                _old_THIS = THIS;               \
                THIS = _xl;                     \
                                                \
                ret = _xl->notify (_xl, params);\
                                                \
                THIS = _old_THIS;               \
        } while (0);

int32_t xlator_set_type_virtual (xlator_t *xl, const char *type);

int32_t xlator_set_type (xlator_t *xl, const char *type);

int32_t xlator_dynload (xlator_t *xl);

xlator_t *file_to_xlator_tree (glusterfs_ctx_t *ctx,
                               FILE *fp);

int xlator_notify (xlator_t *this, int32_t event, void *data, ...);
int xlator_init (xlator_t *this);
int xlator_destroy (xlator_t *xl);

int32_t xlator_tree_init (xlator_t *xl);
int32_t xlator_tree_free_members (xlator_t *xl);
int32_t xlator_tree_free_memacct (xlator_t *xl);

void xlator_tree_fini (xlator_t *xl);

void xlator_foreach (xlator_t *this,
                     void (*fn) (xlator_t *each,
                                 void *data),
                     void *data);

void xlator_foreach_depth_first (xlator_t *this,
				 void (*fn) (xlator_t *each,
					     void *data),
				 void *data);

xlator_t *xlator_search_by_name (xlator_t *any, const char *name);
xlator_t *get_xlator_by_name (xlator_t *this, char *target);
xlator_t *get_xlator_by_type (xlator_t *this, char *target);

void
xlator_set_inode_lru_limit (xlator_t *this, void *data);

void inode_destroy_notify (inode_t *inode, const char *xlname);

int loc_copy (loc_t *dst, loc_t *src);
int loc_copy_overload_parent (loc_t *dst,
                              loc_t *src, inode_t *parent);
#define loc_dup(src, dst) loc_copy(dst, src)
void loc_wipe (loc_t *loc);
int loc_path (loc_t *loc, const char *bname);
void loc_gfid (loc_t *loc, uuid_t gfid);
void loc_pargfid (loc_t *loc, uuid_t pargfid);
char* loc_gfid_utoa (loc_t *loc);
gf_boolean_t loc_is_root (loc_t *loc);
int32_t loc_build_child (loc_t *child, loc_t *parent, char *name);
int xlator_mem_acct_init (xlator_t *xl, int num_types);
int is_gf_log_command (xlator_t *trans, const char *name, char *value);
int glusterd_check_log_level (const char *value);
int xlator_volopt_dynload (char *xlator_type, void **dl_handle,
                           volume_opt_list_t *vol_opt_handle);
enum gf_hdsk_event_notify_op {
        GF_EN_DEFRAG_STATUS,
        GF_EN_MAX,
};
gf_boolean_t
is_graph_topology_equal (glusterfs_graph_t *graph1, glusterfs_graph_t *graph2);
int
glusterfs_volfile_reconfigure (int oldvollen, FILE *newvolfile_fp,
                               glusterfs_ctx_t *ctx, const char *oldvolfile);

int
loc_touchup (loc_t *loc, const char *name);

int
glusterfs_leaf_position(xlator_t *tgt);

int
glusterfs_reachable_leaves(xlator_t *base, dict_t *leaves);

int
xlator_subvolume_count (xlator_t *this);

void xlator_init_lock (void);
void xlator_init_unlock (void);
int
copy_opts_to_child (xlator_t *src, xlator_t *dst, char *glob);

#endif /* _XLATOR_H */
