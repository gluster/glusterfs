/*
   Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef __META_H__
#define __META_H__

#include "strfd.h"

#define DEFAULT_META_DIR_NAME ".meta"

#define META_ROOT_GFID "ba926388-bb9c-4eec-ad60-79dba4cc083a"

#define IS_META_ROOT_GFID(g) (strcmp (uuid_utoa(g), META_ROOT_GFID) == 0)

typedef int (*meta_hook_t) (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata);

typedef struct {
	dict_t *xdata;
} meta_local_t;

typedef struct {
	char *meta_dir_name;
} meta_priv_t;

struct meta_dirent {
	const char *name;
	ia_type_t type;
	meta_hook_t hook;
};

#define DOT_DOTDOT { .name = ".", .type = IA_IFDIR }, { .name = "..", .type = IA_IFDIR }

struct meta_ops {
	struct meta_dirent *fixed_dirents;
	int (*dir_fill) (xlator_t *this, inode_t *dir, struct meta_dirent **entries);
	int (*file_fill) (xlator_t *this, inode_t *file, strfd_t *strfd);
	int (*iatt_fill) (xlator_t *this, inode_t *inode, struct iatt *iatt);
	int (*link_fill) (xlator_t *this, inode_t *inode, strfd_t *strfd);
	int (*file_write) (xlator_t *this, fd_t *fd, struct iovec *iov, int count);
	struct xlator_fops fops;
	struct xlator_cbks cbks;
};

typedef struct {
	char *data;
	struct meta_dirent *dirents;
	size_t size;
} meta_fd_t;


#define COUNT(arr) (sizeof(arr)/sizeof(arr[0]))

#define META_HOOK(loc) (__is_root_gfid (loc->pargfid) && !strcmp (loc->name, META_PRIV(THIS)->meta_dir_name))

#define META_PRIV(t) ((meta_priv_t *)(t->private))

#define META_STACK_UNWIND(fop, frame, params ...)		\
        do {                                                    \
                meta_local_t *__local = NULL;			\
                xlator_t    *__this = NULL;                     \
                if (frame) {                                    \
                        __local = frame->local;                 \
                        __this = frame->this;                   \
                        frame->local = NULL;                    \
                }                                               \
                STACK_UNWIND_STRICT (fop, frame, params);       \
                if (__local) {                                  \
                        meta_local_cleanup (__local, __this);	\
                }                                               \
        } while (0)


#define META_FOP(i, fop, fr, t, params ...) {	\
	struct xlator_fops *_fops = NULL;	\
						\
	_fops = meta_fops_get (i, t);		\
						\
	_fops->fop (fr, t, params);		\
	} while (0)


void meta_iatt_fill (struct iatt *iatt, inode_t *inode, ia_type_t type);

int meta_inode_discover (call_frame_t *frame, xlator_t *this, loc_t *loc,
			 dict_t *xdata);

int meta_ops_set (inode_t *inode, xlator_t *this, struct meta_ops *ops);

struct xlator_fops *meta_fops_get (inode_t *inode, xlator_t *this);
struct xlator_cbks *meta_cbks_get (inode_t *inode, xlator_t *this);
struct meta_ops *meta_ops_get (inode_t *inode, xlator_t *this);

int meta_ctx_set (inode_t *inode, xlator_t *this, void *ctx);

void *meta_ctx_get (inode_t *inode, xlator_t *this);


void meta_local_cleanup (meta_local_t *local, xlator_t *this);

struct xlator_fops *meta_defaults_init (struct xlator_fops *fops);

meta_fd_t *meta_fd_get (fd_t *fd, xlator_t *this);

int meta_fd_release (fd_t *fd, xlator_t *this);

dict_t *meta_direct_io_mode (dict_t *xdata, call_frame_t *frame);

meta_local_t *meta_local (call_frame_t *frame);

int meta_file_fill (xlator_t *this, fd_t *fd);

int meta_dir_fill (xlator_t *this, fd_t *fd);

int fixed_dirents_len (struct meta_dirent *dirents);
#endif /* __META_H__ */
