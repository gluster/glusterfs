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

#ifndef _DHT_H
#define _DHT_H


typedef int (*dht_selfheal_dir_cbk_t) (call_frame_t *frame, void *cookie,
				       xlator_t *this,
				       int32_t op_ret, int32_t op_errno);


struct dht_layout {
        int               cnt;
	int               preset;
        int               gen;
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
typedef struct dht_layout dht_layout_t;


struct dht_local {
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
	dht_layout_t            *layout;
	size_t                   size;
	ino_t                    st_ino;
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
		dht_selfheal_dir_cbk_t   dir_cbk;
		dht_layout_t    *layout;
	} selfheal;

	/* needed by nufa */
	int32_t flags;
	mode_t  mode;
	dev_t   rdev;
};
typedef struct dht_local dht_local_t;


struct dht_conf {
	gf_lock_t      subvolume_lock;
        int            subvolume_cnt;
        xlator_t     **subvolumes;
	xlator_t      *local_volume;     /* Needed by NUFA */
	char          *subvolume_status;
	dht_layout_t **file_layouts;
	dht_layout_t **dir_layouts;
	dht_layout_t  *default_dir_layout;
	int            search_unhashed;
	int            gen;
};
typedef struct dht_conf dht_conf_t;


struct dht_disk_layout {
	uint32_t           cnt;
	uint32_t           type;
	struct {
		uint32_t   start;
		uint32_t   stop;
	} list[1];
};
typedef struct dht_disk_layout dht_disk_layout_t;
 
#define ENTRY_MISSING(op_ret, op_errno) (op_ret == -1 && op_errno == ENOENT)

#define is_fs_root(loc) (strcmp (loc->path, "/") == 0)

#define is_revalidate(loc) (dict_get (loc->inode->ctx, this->name) != NULL)

#define is_last_call(cnt) (cnt == 0)

#define DHT_LINKFILE_MODE (S_ISVTX)
#define check_is_linkfile(i,s,x) ((s->st_mode & ~S_IFMT) == DHT_LINKFILE_MODE)

#define check_is_dir(i,s,x) (S_ISDIR(s->st_mode))

#define layout_is_sane(layout) ((layout) && (layout->cnt > 0))

#define DHT_STACK_UNWIND(frame, params ...) do {       \
		dht_local_t *__local = NULL;           \
		__local = frame->local;                \
		frame->local = NULL;		       \
		STACK_UNWIND (frame, params);          \
		dht_local_wipe (__local);	       \
	} while (0)

#define DHT_STACK_DESTROY(frame) do {		       \
		dht_local_t *__local = NULL;           \
		__local = frame->local;                \
		frame->local = NULL;		       \
		STACK_DESTROY (frame->root);	       \
		dht_local_wipe (__local);	       \
	} while (0)

dht_layout_t *dht_layout_new (xlator_t *this, int cnt);
dht_layout_t *dht_layout_get (xlator_t *this, inode_t *inode);
dht_layout_t *dht_layout_for_subvol (xlator_t *this, xlator_t *subvol);
xlator_t *dht_layout_search (xlator_t *this, dht_layout_t *layout,
			     const char *name);
int dht_layout_normalize (xlator_t *this, loc_t *loc, dht_layout_t *layout);
int dht_layout_anomalies (xlator_t *this, loc_t *loc, dht_layout_t *layout,
			  uint32_t *holes_p, uint32_t *overlaps_p,
			  uint32_t *missing_p, uint32_t *down_p,
			  uint32_t *misc_p);

xlator_t *dht_linkfile_subvol (xlator_t *this, inode_t *inode,
			       struct stat *buf, dict_t *xattr);
int dht_linkfile_unlink (call_frame_t *frame, xlator_t *this,
			 xlator_t *subvol, loc_t *loc);

int dht_layouts_init (xlator_t *this, dht_conf_t *conf);
int dht_layout_merge (xlator_t *this, dht_layout_t *layout, xlator_t *subvol,
		      int op_ret, int op_errno, dict_t *xattr);

int dht_disk_layout_extract (xlator_t *this, dht_layout_t *layout,
			     int pos, int32_t **disk_layout_p);
int dht_disk_layout_merge (xlator_t *this, dht_layout_t *layout,
			   int pos, int32_t *disk_layout);


int dht_frame_return (call_frame_t *frame);

int dht_itransform (xlator_t *this, xlator_t *subvol, uint64_t x, uint64_t *y);
int dht_deitransform (xlator_t *this, uint64_t y, xlator_t **subvol,
		      uint64_t *x);

void dht_local_wipe (dht_local_t *local);
dht_local_t *dht_local_init (call_frame_t *frame);
int dht_stat_merge (xlator_t *this, struct stat *to, struct stat *from,
		    xlator_t *subvol);

xlator_t *dht_subvol_get_hashed (xlator_t *this, loc_t *loc);
xlator_t *dht_subvol_get_cached (xlator_t *this, inode_t *inode);
xlator_t *dht_subvol_next (xlator_t *this, xlator_t *prev);
int dht_subvol_cnt (xlator_t *this, xlator_t *subvol);

int dht_hash_compute (int type, const char *name, uint32_t *hash_p);

int dht_linkfile_create (call_frame_t *frame, fop_mknod_cbk_t linkfile_cbk,
			 xlator_t *tovol, xlator_t *fromvol, loc_t *loc);
int
dht_selfheal_directory (call_frame_t *frame, dht_selfheal_dir_cbk_t cbk,
			loc_t *loc, dht_layout_t *layout);
int
dht_selfheal_restore (call_frame_t *frame, dht_selfheal_dir_cbk_t cbk,
		      loc_t *loc, dht_layout_t *layout);

int inode_ctx_set (inode_t *inode, xlator_t *this, void *ctx);
int inode_ctx_get (inode_t *inode, xlator_t *this, void **ctx);

int dht_rename (call_frame_t *frame, xlator_t *this,
		loc_t *oldloc, loc_t *newloc);
#endif /* _DHT_H */
