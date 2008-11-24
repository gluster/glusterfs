/*
   Copyright (c) 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
   This file is part of GlusterFS.

   GlusterFS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 3 of the License,
   or (at your option) any later version.

   GlusterFS is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see
   <http://www.gnu.org/licenses/>.
*/

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#ifndef _NUFA_H
#define _NUFA_H


typedef int (*nufa_selfheal_dir_cbk_t) (call_frame_t *frame, void *cookie,
				       xlator_t *this,
				       int32_t op_ret, int32_t op_errno);


struct nufa_layout {
        int               cnt;
	int               preset;
	int               type;
        struct {
		int       err;   /* 0 = normal
				   -1 = dir exists and no xattr
				   >0 = dir lookup failed with errno
				 */
                uint32_t  start;
                uint32_t  stop;
                xlator_t *xlator;
        } list[0];
};
typedef struct nufa_layout nufa_layout_t;


struct nufa_local {
	int                      call_cnt;
	loc_t                    loc;
	loc_t                    loc2;
	int                      op_ret;
	int                      op_errno;
	struct stat              stbuf;
	struct statvfs           statvfs;
	fd_t                    *fd;
	inode_t                 *inode;
	dict_t                  *xattr;
	nufa_layout_t            *layout;
	mode_t                   mode;
	size_t                   size;
	ino_t                    st_ino;
	dev_t                    rdev;
	int32_t                  flags;
	xlator_t                *src_hashed, *src_cached;
	xlator_t                *dst_hashed, *dst_cached;
	xlator_t                *cached_subvol;
	xlator_t                *hashed_subvol;
	char                     need_selfheal;
	struct {
		fop_mknod_cbk_t  linkfile_cbk;
		struct stat      stbuf;
		loc_t            loc;
		inode_t         *inode;
		dict_t          *xattr;
		xlator_t        *srcvol;
	} linkfile;
	struct {
		uint32_t         hole_cnt;
		uint32_t         overlaps_cnt;
		uint32_t         missing;
		uint32_t         down;
		uint32_t         misc;
		nufa_selfheal_dir_cbk_t   dir_cbk;
		nufa_layout_t    *layout;
	} selfheal;
};
typedef struct nufa_local nufa_local_t;


struct nufa_conf {
	gf_lock_t      subvolume_lock;
        int            subvolume_cnt;
        xlator_t     **subvolumes;
	char          *subvolume_status;
	nufa_layout_t **file_layouts;
	nufa_layout_t **dir_layouts;
	nufa_layout_t  *default_dir_layout;
	xlator_t      *local_volume;
};
typedef struct nufa_conf nufa_conf_t;


struct nufa_disk_layout {
	uint32_t           cnt;
	uint32_t           type;
	struct {
		uint32_t   start;
		uint32_t   stop;
	} list[1];
};
typedef struct nufa_disk_layout nufa_disk_layout_t;
 

#define is_fs_root(loc) (strcmp (loc->path, "/") == 0)

#define is_revalidate(loc) (dict_get (loc->inode->ctx, this->name) != NULL)

#define is_last_call(cnt) (cnt == 0)

#define NUFA_LINKFILE_MODE (S_ISVTX)
#define check_is_linkfile(i,s,x) ((s->st_mode & ~S_IFMT) == NUFA_LINKFILE_MODE)

#define check_is_dir(i,s,x) (S_ISDIR(s->st_mode))

#define layout_is_sane(layout) ((layout) && (layout->cnt > 0))

#define NUFA_STACK_UNWIND(frame, params ...) do {       \
		nufa_local_t *__local = NULL;          \
		__local = frame->local;                \
		frame->local = NULL;		       \
		STACK_UNWIND (frame, params);          \
		nufa_local_wipe (__local);	       \
	} while (0)


nufa_layout_t *nufa_layout_new (xlator_t *this, int cnt);
nufa_layout_t *nufa_layout_get (xlator_t *this, inode_t *inode);
nufa_layout_t *nufa_layout_for_subvol (xlator_t *this, xlator_t *subvol);
xlator_t *nufa_layout_search (xlator_t *this, nufa_layout_t *layout,
			     const char *name);
int nufa_layout_normalize (xlator_t *this, loc_t *loc, nufa_layout_t *layout);
int nufa_layout_anomalies (xlator_t *this, loc_t *loc, nufa_layout_t *layout,
			  uint32_t *holes_p, uint32_t *overlaps_p,
			  uint32_t *missing_p, uint32_t *down_p,
			  uint32_t *misc_p);

xlator_t *nufa_linkfile_subvol (xlator_t *this, inode_t *inode,
			       struct stat *buf, dict_t *xattr);

int nufa_layouts_init (xlator_t *this, nufa_conf_t *conf);
int nufa_layout_merge (xlator_t *this, nufa_layout_t *layout, xlator_t *subvol,
		      int op_ret, int op_errno, dict_t *xattr);

int nufa_disk_layout_extract (xlator_t *this, nufa_layout_t *layout,
			     int pos, int32_t **disk_layout_p);
int nufa_disk_layout_merge (xlator_t *this, nufa_layout_t *layout,
			   int pos, int32_t *disk_layout);


int nufa_frame_return (call_frame_t *frame);

int nufa_itransform (xlator_t *this, xlator_t *subvol, uint64_t x, uint64_t *y);
int nufa_deitransform (xlator_t *this, uint64_t y, xlator_t **subvol,
		      uint64_t *x);

void nufa_local_wipe (nufa_local_t *local);
nufa_local_t *nufa_local_init (call_frame_t *frame);
int nufa_stat_merge (xlator_t *this, struct stat *to, struct stat *from,
		    xlator_t *subvol);

xlator_t *nufa_subvol_get_hashed (xlator_t *this, loc_t *loc);
xlator_t *nufa_subvol_get_cached (xlator_t *this, inode_t *inode);
xlator_t *nufa_subvol_next (xlator_t *this, xlator_t *prev);
int nufa_subvol_cnt (xlator_t *this, xlator_t *subvol);

int nufa_hash_compute (int type, const char *name, uint32_t *hash_p);

int nufa_linkfile_create (call_frame_t *frame, fop_mknod_cbk_t linkfile_cbk,
			 xlator_t *srcvol, xlator_t *dstvol, loc_t *loc);

int
nufa_selfheal_directory (call_frame_t *frame, nufa_selfheal_dir_cbk_t cbk,
			loc_t *loc, nufa_layout_t *layout);
int
nufa_selfheal_restore (call_frame_t *frame, nufa_selfheal_dir_cbk_t cbk,
		      loc_t *loc, nufa_layout_t *layout);

int inode_ctx_set (inode_t *inode, xlator_t *this, void *ctx);
int inode_ctx_get (inode_t *inode, xlator_t *this, void **ctx);

int nufa_rename (call_frame_t *frame, xlator_t *this,
		loc_t *oldloc, loc_t *newloc);

#endif /* _NUFA_H */
