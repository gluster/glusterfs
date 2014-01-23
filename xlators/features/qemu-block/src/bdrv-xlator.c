/*
  Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "inode.h"
#include "syncop.h"
#include "qemu-block.h"
#include "block/block_int.h"

typedef struct BDRVGlusterState {
	inode_t *inode;
} BDRVGlusterState;

static QemuOptsList runtime_opts = {
	.name = "gluster",
	.head = QTAILQ_HEAD_INITIALIZER(runtime_opts.head),
	.desc = {
		{
			.name = "filename",
			.type = QEMU_OPT_STRING,
			.help = "GFID of file",
		},
		{ /* end of list */ }
	},
};

inode_t *
qb_inode_from_filename (const char *filename)
{
	const char *iptr = NULL;
	inode_t *inode = NULL;

	iptr = filename + 17;
	sscanf (iptr, "%p", &inode);

	return inode;
}


int
qb_inode_to_filename (inode_t *inode, char *filename, int size)
{
	return snprintf (filename, size, "gluster://inodep:%p", inode);
}


static fd_t *
fd_from_bs (BlockDriverState *bs)
{
	BDRVGlusterState *s = bs->opaque;

	return fd_anonymous (s->inode);
}


static int
qemu_gluster_open (BlockDriverState *bs, QDict *options, int bdrv_flags)
{
	inode_t *inode = NULL;
	BDRVGlusterState *s = bs->opaque;
	QemuOpts *opts = NULL;
	Error *local_err = NULL;
	const char *filename = NULL;
	char gfid_str[128];
	int ret;
	qb_conf_t *conf = THIS->private;

	opts = qemu_opts_create_nofail(&runtime_opts);
	qemu_opts_absorb_qdict(opts, options, &local_err);
	if (error_is_set(&local_err)) {
		qerror_report_err(local_err);
		error_free(local_err);
		return -EINVAL;
	}

	filename = qemu_opt_get(opts, "filename");

	/*
	 * gfid:<gfid> format means we're opening a backing image.
	 */
	ret = sscanf(filename, "gluster://gfid:%s", gfid_str);
	if (ret) {
		loc_t loc = {0,};
		struct iatt buf = {0,};
		uuid_t gfid;

		uuid_parse(gfid_str, gfid);

		loc.inode = inode_find(conf->root_inode->table, gfid);
		if (!loc.inode) {
			loc.inode = inode_new(conf->root_inode->table);
			uuid_copy(loc.inode->gfid, gfid);
		}

		uuid_copy(loc.gfid, loc.inode->gfid);
		ret = syncop_lookup(FIRST_CHILD(THIS), &loc, NULL, &buf, NULL,
				    NULL);
		if (ret) {
			loc_wipe(&loc);
			return ret;
		}

		s->inode = inode_ref(loc.inode);
		loc_wipe(&loc);
	} else {
		inode = qb_inode_from_filename (filename);
		if (!inode)
			return -EINVAL;

		s->inode = inode_ref(inode);
	}

	return 0;
}


static int
qemu_gluster_create (const char *filename, QEMUOptionParameter *options)
{
	uint64_t total_size = 0;
	inode_t *inode = NULL;
	fd_t *fd = NULL;
	struct iatt stat = {0, };
	int ret = 0;

	inode = qb_inode_from_filename (filename);
	if (!inode)
		return -EINVAL;

	while (options && options->name) {
		if (!strcmp(options->name, BLOCK_OPT_SIZE)) {
			total_size = options->value.n / BDRV_SECTOR_SIZE;
		}
		options++;
	}

	fd = fd_anonymous (inode);
	if (!fd)
		return -ENOMEM;

	ret = syncop_fstat (FIRST_CHILD(THIS), fd, &stat);
	if (ret) {
		fd_unref (fd);
		return ret;
	}

	if (stat.ia_size) {
		/* format ONLY if the filesize is 0 bytes */
		fd_unref (fd);
		return -EFBIG;
	}

	if (total_size) {
		ret = syncop_ftruncate (FIRST_CHILD(THIS), fd, total_size);
		if (ret) {
			fd_unref (fd);
			return ret;
		}
	}

	fd_unref (fd);
	return 0;
}


static int
qemu_gluster_co_readv (BlockDriverState *bs, int64_t sector_num, int nb_sectors,
		       QEMUIOVector *qiov)
{
	fd_t *fd = NULL;
	off_t offset = 0;
	size_t size = 0;
	struct iovec *iov = NULL;
	int count = 0;
	struct iobref *iobref = NULL;
	int ret = 0;

	fd = fd_from_bs (bs);
	if (!fd)
		return -EIO;

	offset = sector_num * BDRV_SECTOR_SIZE;
	size = nb_sectors * BDRV_SECTOR_SIZE;

	ret = syncop_readv (FIRST_CHILD(THIS), fd, size, offset, 0,
			    &iov, &count, &iobref);
	if (ret < 0)
		goto out;

	iov_copy (qiov->iov, qiov->niov, iov, count); /* *choke!* */

out:
	GF_FREE (iov);
	if (iobref)
		iobref_unref (iobref);
	fd_unref (fd);
	return ret;
}


static int
qemu_gluster_co_writev (BlockDriverState *bs, int64_t sector_num, int nb_sectors,
			QEMUIOVector *qiov)
{
	fd_t *fd = NULL;
	off_t offset = 0;
	size_t size = 0;
	struct iobref *iobref = NULL;
	struct iobuf *iobuf = NULL;
	struct iovec iov = {0, };
	int ret = -ENOMEM;

	fd = fd_from_bs (bs);
	if (!fd)
		return -EIO;

	offset = sector_num * BDRV_SECTOR_SIZE;
	size = nb_sectors * BDRV_SECTOR_SIZE;

	iobuf = iobuf_get2 (THIS->ctx->iobuf_pool, size);
	if (!iobuf)
		goto out;

	iobref = iobref_new ();
	if (!iobref) {
		goto out;
	}

	iobref_add (iobref, iobuf);

	iov_unload (iobuf_ptr (iobuf), qiov->iov, qiov->niov); /* *choke!* */

	iov.iov_base = iobuf_ptr (iobuf);
	iov.iov_len = size;

	ret = syncop_writev (FIRST_CHILD(THIS), fd, &iov, 1, offset, iobref, 0);

out:
	if (iobuf)
		iobuf_unref (iobuf);
	if (iobref)
		iobref_unref (iobref);
	fd_unref (fd);
	return ret;
}


static int
qemu_gluster_co_flush (BlockDriverState *bs)
{
	fd_t *fd = NULL;
	int ret = 0;

	fd = fd_from_bs (bs);

	ret = syncop_flush (FIRST_CHILD(THIS), fd);

	fd_unref (fd);

	return ret;
}


static int
qemu_gluster_co_fsync (BlockDriverState *bs)
{
	fd_t *fd = NULL;
	int ret = 0;

	fd = fd_from_bs (bs);

	ret = syncop_fsync (FIRST_CHILD(THIS), fd, 0);

	fd_unref (fd);

	return ret;
}


static int
qemu_gluster_truncate (BlockDriverState *bs, int64_t offset)
{
	fd_t *fd = NULL;
	int ret = 0;

	fd = fd_from_bs (bs);

	ret = syncop_ftruncate (FIRST_CHILD(THIS), fd, offset);

	fd_unref (fd);

	return ret;
}


static int64_t
qemu_gluster_getlength (BlockDriverState *bs)
{
	fd_t *fd = NULL;
	int ret = 0;
	struct iatt iatt = {0, };

	fd = fd_from_bs (bs);

	ret = syncop_fstat (FIRST_CHILD(THIS), fd, &iatt);
	if (ret < 0)
		return -1;

	return iatt.ia_size;
}


static int64_t
qemu_gluster_allocated_file_size (BlockDriverState *bs)
{
	fd_t *fd = NULL;
	int ret = 0;
	struct iatt iatt = {0, };

	fd = fd_from_bs (bs);

	ret = syncop_fstat (FIRST_CHILD(THIS), fd, &iatt);
	if (ret < 0)
		return -1;

	return iatt.ia_blocks * 512;
}


static void
qemu_gluster_close (BlockDriverState *bs)
{
	BDRVGlusterState *s = NULL;

	s = bs->opaque;

	inode_unref (s->inode);

	return;
}


static QEMUOptionParameter qemu_gluster_create_options[] = {
	{
		.name = BLOCK_OPT_SIZE,
		.type = OPT_SIZE,
		.help = "Virtual disk size"
	},
	{ NULL }
};


static BlockDriver bdrv_gluster = {
	.format_name                  = "gluster",
	.protocol_name                = "gluster",
	.instance_size                = sizeof(BDRVGlusterState),
	.bdrv_file_open               = qemu_gluster_open,
	.bdrv_close                   = qemu_gluster_close,
	.bdrv_create                  = qemu_gluster_create,
	.bdrv_getlength               = qemu_gluster_getlength,
	.bdrv_get_allocated_file_size = qemu_gluster_allocated_file_size,
	.bdrv_co_readv                = qemu_gluster_co_readv,
	.bdrv_co_writev               = qemu_gluster_co_writev,
	.bdrv_co_flush_to_os          = qemu_gluster_co_flush,
	.bdrv_co_flush_to_disk        = qemu_gluster_co_fsync,
	.bdrv_truncate                = qemu_gluster_truncate,
	.create_options               = qemu_gluster_create_options,
};


static void bdrv_gluster_init(void)
{
	bdrv_register(&bdrv_gluster);
}


block_init(bdrv_gluster_init);
