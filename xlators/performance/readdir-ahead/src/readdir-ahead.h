/*
  Copyright (c) 2008-2013 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __READDIR_AHEAD_H
#define __READDIR_AHEAD_H

/* state flags */
#define RDA_FD_NEW	(1 << 0)
#define RDA_FD_RUNNING	(1 << 1)
#define RDA_FD_EOD	(1 << 2)
#define RDA_FD_ERROR	(1 << 3)
#define RDA_FD_BYPASS	(1 << 4)
#define RDA_FD_PLUGGED	(1 << 5)

struct rda_fd_ctx {
	off_t cur_offset;	/* current head of the ctx */
	size_t cur_size;	/* current size of the preload */
	off_t next_offset;	/* tail of the ctx */
	uint32_t state;
	gf_lock_t lock;
	gf_dirent_t entries;
	call_frame_t *fill_frame;
	call_stub_t *stub;
	int op_errno;
        dict_t *xattrs;      /* md-cache keys to be sent in readdirp() */
};

struct rda_local {
	struct rda_fd_ctx *ctx;
	fd_t *fd;
	off_t offset;
        dict_t *xattrs;      /* md-cache keys to be sent in readdirp() */
        int32_t skip_dir;
};

struct rda_priv {
	uint64_t rda_req_size;
	uint64_t rda_low_wmark;
	uint64_t rda_high_wmark;
        uint64_t rda_cache_limit;
        uint64_t rda_cache_size;
};

#endif /* __READDIR_AHEAD_H */
