/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/
#include <ctype.h>
#include <sys/uio.h>

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include "xlator.h"
#include "logging.h"
#include "defaults.h"

#include "crypt-common.h"
#include "crypt.h"

static void init_inode_info_head(struct crypt_inode_info *info, fd_t *fd);
static int32_t init_inode_info_tail(struct crypt_inode_info *info,
				    struct master_cipher_info *master);
static int32_t prepare_for_submit_hole(call_frame_t *frame, xlator_t *this,
				       uint64_t from, off_t size);
static int32_t load_file_size(call_frame_t *frame, void *cookie,
			      xlator_t *this, int32_t op_ret, int32_t op_errno,
			      dict_t *dict, dict_t *xdata);
static void do_ordered_submit(call_frame_t *frame, xlator_t *this,
			      atom_data_type dtype);
static void do_parallel_submit(call_frame_t *frame, xlator_t *this,
			       atom_data_type dtype);
static void put_one_call_open(call_frame_t *frame);
static void put_one_call_readv(call_frame_t *frame, xlator_t *this);
static void put_one_call_writev(call_frame_t *frame, xlator_t *this);
static void put_one_call_ftruncate(call_frame_t *frame, xlator_t *this);
static void free_avec(struct iovec *avec, char **pool, int blocks_in_pool);
static void free_avec_data(crypt_local_t *local);
static void free_avec_hole(crypt_local_t *local);

static crypt_local_t *crypt_alloc_local(call_frame_t *frame, xlator_t *this,
					glusterfs_fop_t fop)
{
	crypt_local_t *local = NULL;

	local = mem_get0(this->local_pool);
	if (!local) {
		gf_log(this->name, GF_LOG_ERROR, "out of memory");
		return NULL;
	}
	local->fop = fop;
	LOCK_INIT(&local->hole_lock);
	LOCK_INIT(&local->call_lock);
	LOCK_INIT(&local->rw_count_lock);

	frame->local = local;
	return local;
}

struct crypt_inode_info *get_crypt_inode_info(inode_t *inode, xlator_t *this)
{
	int ret;
	uint64_t value = 0;
	struct crypt_inode_info *info;

	ret = inode_ctx_get(inode, this, &value);
	if (ret == -1) {
		gf_log (this->name, GF_LOG_WARNING,
			"Can not get inode info");
		return NULL;
	}
	info = (struct crypt_inode_info *)(long)value;
	if (info == NULL) {
		gf_log (this->name, GF_LOG_WARNING,
			"Can not obtain inode info");
		return NULL;
	}
	return info;
}

static struct crypt_inode_info *local_get_inode_info(crypt_local_t *local,
						      xlator_t *this)
{
	if (local->info)
		return local->info;
	local->info = get_crypt_inode_info(local->fd->inode, this);
	return local->info;
}

static struct crypt_inode_info *alloc_inode_info(crypt_local_t *local,
						 loc_t *loc)
{
	struct crypt_inode_info *info;

	info = GF_CALLOC(1, sizeof(*info), gf_crypt_mt_inode);
	if (!info) {
		local->op_ret = -1;
		local->op_errno = ENOMEM;
		gf_log ("crypt", GF_LOG_WARNING,
			"Can not allocate inode info");
		return NULL;
	}
	memset(info, 0, sizeof(*info));
#if DEBUG_CRYPT
	info->loc = GF_CALLOC(1, sizeof(*loc), gf_crypt_mt_loc);
	if (!info->loc) {
		gf_log("crypt", GF_LOG_WARNING, "Can not allocate loc");
		GF_FREE(info);
		return NULL;
	}
	if (loc_copy(info->loc, loc)){
		GF_FREE(info->loc);
		GF_FREE(info);
		return NULL;
	}
#endif /* DEBUG_CRYPT */

	local->info = info;
	return info;
}

static void free_inode_info(struct crypt_inode_info *info)
{
#if DEBUG_CRYPT
	loc_wipe(info->loc);
	GF_FREE(info->loc);
#endif
	memset(info, 0, sizeof(*info));
	GF_FREE(info);
}

int crypt_forget (xlator_t *this, inode_t *inode)
{
        uint64_t ctx_addr = 0;
        if (!inode_ctx_del (inode, this, &ctx_addr))
                free_inode_info((struct crypt_inode_info *)(long)ctx_addr);
        return 0;
}

#if DEBUG_CRYPT
static void check_read(call_frame_t *frame, xlator_t *this, int32_t read,
		       struct iovec *vec, int32_t count, struct iatt *stbuf)
{
	crypt_local_t *local = frame->local;
	struct object_cipher_info *object = get_object_cinfo(local->info);
	struct avec_config *conf = &local->data_conf;
	uint32_t resid = stbuf->ia_size & (object_alg_blksize(object) - 1);

	if (read <= 0)
		return;
	if (read != iovec_get_size(vec, count))
		gf_log ("crypt", GF_LOG_DEBUG,
			"op_ret differs from amount of read bytes");

	if (object_alg_should_pad(object) && (read & (object_alg_blksize(object) - 1)))
		gf_log ("crypt", GF_LOG_DEBUG,
			"bad amount of read bytes (!= 0 mod(cblock size))");

	if (conf->aligned_offset + read >
	    stbuf->ia_size + (resid ? object_alg_blksize(object) - resid : 0))
		gf_log ("crypt", GF_LOG_DEBUG,
			"bad amount of read bytes (too large))");

}

#define PT_BYTES_TO_DUMP (32)
static void dump_plain_text(crypt_local_t *local, struct iovec *avec)
{
	int32_t to_dump;
	char str[PT_BYTES_TO_DUMP + 1];

	if (!avec)
		return;
	to_dump = avec->iov_len;
	if (to_dump > PT_BYTES_TO_DUMP)
		to_dump = PT_BYTES_TO_DUMP;
	memcpy(str, avec->iov_base, to_dump);
	memset(str + to_dump, '0', 1);
	gf_log("crypt", GF_LOG_DEBUG, "Read file: %s", str);
}

static int32_t data_conf_invariant(struct avec_config *conf)
{
	return conf->acount ==
		!!has_head_block(conf) +
		!!has_tail_block(conf)+
		conf->nr_full_blocks;
}

static int32_t hole_conf_invariant(struct avec_config *conf)
{
	return conf->blocks_in_pool ==
		!!has_head_block(conf) +
		!!has_tail_block(conf)+
		!!has_full_blocks(conf);
}

static void crypt_check_conf(struct avec_config *conf)
{
	int32_t ret = 0;
	const char *msg;

	switch (conf->type) {
	case DATA_ATOM:
		msg = "data";
		ret = data_conf_invariant(conf);
		break;
	case HOLE_ATOM:
		msg = "hole";
		ret = hole_conf_invariant(conf);
		break;
	default:
		msg = "unknown";
	}
	if (!ret)
		gf_log("crypt", GF_LOG_DEBUG, "bad %s conf", msg);
}

static void check_buf(call_frame_t *frame, xlator_t *this, struct iatt *buf)
{
	crypt_local_t *local = frame->local;
	struct object_cipher_info *object = &local->info->cinfo;
	uint64_t local_file_size;

	switch(local->fop) {
	case GF_FOP_FTRUNCATE:
		return;
	case GF_FOP_WRITE:
		local_file_size = local->new_file_size;
		break;
	case GF_FOP_READ:
		if (parent_is_crypt_xlator(frame, this))
			return;
		local_file_size = local->cur_file_size;
		break;
	default:
		gf_log("crypt", GF_LOG_DEBUG, "bad file operation");
		return;
	}
	if (buf->ia_size != round_up(local_file_size,
					 object_alg_blksize(object)))
		gf_log("crypt", GF_LOG_DEBUG,
		       "bad ia_size in buf (%llu), should be %llu",
		       (unsigned long long)buf->ia_size,
		       (unsigned long long)round_up(local_file_size,
						    object_alg_blksize(object)));
}

#else
#define check_read(frame, this, op_ret, vec, count, stbuf) noop
#define dump_plain_text(local, avec) noop
#define crypt_check_conf(conf) noop
#define check_buf(frame, this, buf) noop
#endif /* DEBUG_CRYPT */

/*
 * Pre-conditions:
 * @vec represents a ciphertext of expanded size and
 * aligned offset.
 *
 * Compound a temporal vector @avec with block-aligned
 * components, decrypt and fix it up to represent a chunk
 * of data corresponding to the original size and offset.
 * Pass the result to the next translator.
 */
int32_t crypt_readv_cbk(call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno,
			struct iovec *vec,
			int32_t count,
			struct iatt *stbuf,
			struct iobref *iobref,
			dict_t *xdata)
{
	crypt_local_t *local = frame->local;
	struct avec_config *conf = &local->data_conf;
	struct object_cipher_info *object = &local->info->cinfo;

	struct iovec *avec;
	uint32_t i;
	uint32_t to_vec;
	uint32_t to_user;

	check_buf(frame, this, stbuf);
	check_read(frame, this, op_ret, vec, count, stbuf);

	local->op_ret = op_ret;
	local->op_errno = op_errno;
	local->iobref = iobref_ref(iobref);

	local->buf = *stbuf;
	local->buf.ia_size = local->cur_file_size;

	if (op_ret <= 0 || count == 0 || vec[0].iov_len == 0)
		goto put_one_call;

	if (conf->orig_offset >= local->cur_file_size) {
		local->op_ret = 0;
		goto put_one_call;
	}
	/* 
	 * correct config params with real file size
	 * and actual amount of bytes read
	 */
	set_config_offsets(frame, this,
			   conf->orig_offset, op_ret, DATA_ATOM, 0);

	if (conf->orig_offset + conf->orig_size > local->cur_file_size)
		conf->orig_size = local->cur_file_size - conf->orig_offset;
	/*
	 * calculate amount of data to be returned
	 * to user.
	 */
	to_user = op_ret;
	if (conf->aligned_offset + to_user <= conf->orig_offset) {
		gf_log(this->name, GF_LOG_WARNING, "Incomplete read");
		local->op_ret = -1;
		local->op_errno = EIO;
		goto put_one_call;
	}
	to_user -= (conf->aligned_offset - conf->orig_offset);

	if (to_user > conf->orig_size)
		to_user = conf->orig_size;
	local->rw_count = to_user;

	op_errno = set_config_avec_data(this, local,
					conf, object, vec, count);
	if (op_errno) {
		local->op_ret = -1;
		local->op_errno = op_errno;
		goto put_one_call;
	}
	avec = conf->avec;
#if DEBUG_CRYPT
	if (conf->off_in_tail != 0 &&
	    conf->off_in_tail < object_alg_blksize(object) &&
	    object_alg_should_pad(object))
		gf_log(this->name, GF_LOG_DEBUG, "Bad offset in tail %d",
		       conf->off_in_tail);
	if (iovec_get_size(vec, count) != 0 &&
	    in_same_lblock(conf->orig_offset + iovec_get_size(vec, count) - 1,
			   local->cur_file_size - 1,
			   object_alg_blkbits(object))) {
		gf_log(this->name, GF_LOG_DEBUG, "Compound last cblock");
		dump_cblock(this,
			    (unsigned char *)(avec[conf->acount - 1].iov_base) +
			    avec[conf->acount - 1].iov_len - object_alg_blksize(object));
		dump_cblock(this,
			    (unsigned char *)(vec[count - 1].iov_base) +
			    vec[count - 1].iov_len - object_alg_blksize(object));
	}
#endif
	decrypt_aligned_iov(object, avec,
			    conf->acount, conf->aligned_offset);
	/*
	 * pass proper plain data to user
	 */
	avec[0].iov_base += (conf->aligned_offset - conf->orig_offset);
	avec[0].iov_len  -= (conf->aligned_offset - conf->orig_offset);

	to_vec = to_user;
	for (i = 0; i < conf->acount; i++) {
		if (avec[i].iov_len > to_vec)
			avec[i].iov_len = to_vec;
		to_vec -= avec[i].iov_len;
	}
 put_one_call:
	put_one_call_readv(frame, this);
	return 0;
}

static int32_t do_readv(call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno,
			dict_t *dict,
			dict_t *xdata)
{
	data_t *data;
	crypt_local_t *local = frame->local;

	if (op_ret < 0)
		goto error;	
	/*
	 * extract regular file size
	 */
	data = dict_get(dict, FSIZE_XATTR_PREFIX);
	if (!data) {
		gf_log("crypt", GF_LOG_WARNING, "Regular file size not found");
		op_errno = EIO;
		goto error;
	}
	local->cur_file_size = data_to_uint64(data);
	
	get_one_call(frame);
	STACK_WIND(frame,
		   crypt_readv_cbk,
		   FIRST_CHILD (this),
		   FIRST_CHILD (this)->fops->readv,
		   local->fd,
		   /*
		    * FIXME: read amount can be reduced
		    */
		   local->data_conf.expanded_size,
		   local->data_conf.aligned_offset,
		   local->flags,
		   local->xdata);
	return 0;
 error:
	local->op_ret = -1;
	local->op_errno = op_errno;

	get_one_call(frame);
	put_one_call_readv(frame, this);
	return 0;
}

static int32_t crypt_readv_finodelk_cbk(call_frame_t *frame,
					void *cookie,
					xlator_t *this,
					int32_t op_ret,
					int32_t op_errno,
					dict_t *xdata)
{
	crypt_local_t *local = frame->local;

	if (op_ret < 0)
		goto error;
	/*
	 * An access has been granted,
	 * retrieve file size
	 */
	STACK_WIND(frame,
		   do_readv,
		   FIRST_CHILD(this),
		   FIRST_CHILD(this)->fops->fgetxattr,
		   local->fd,
		   FSIZE_XATTR_PREFIX,
		   NULL);
	return  0;
 error:
	fd_unref(local->fd);
	if (local->xdata)
		dict_unref(local->xdata);
	STACK_UNWIND_STRICT(readv,
			    frame,
			    -1,
			    op_errno,
			    NULL,
			    0,
			    NULL,
			    NULL,
			    NULL);
	return 0;
}

static int32_t readv_trivial_completion(call_frame_t *frame,
					void *cookie,
					xlator_t *this,
					int32_t op_ret,
					int32_t op_errno,
					struct iatt *buf,
					dict_t *xdata)
{
	crypt_local_t *local = frame->local;

	local->op_ret = op_ret;
	local->op_errno = op_errno;

	if (op_ret < 0) {
		gf_log(this->name, GF_LOG_WARNING,
		       "stat failed (%d)", op_errno);
		goto error;
	}
	local->buf = *buf;
	STACK_WIND(frame,
		   load_file_size,
		   FIRST_CHILD(this),
		   FIRST_CHILD(this)->fops->getxattr,
		   local->loc,
		   FSIZE_XATTR_PREFIX,
		   NULL);
	return 0;	
 error:
	STACK_UNWIND_STRICT(readv, frame, op_ret, op_errno,
			    NULL, 0, NULL, NULL, NULL);
	return 0;
}

int32_t crypt_readv(call_frame_t *frame,
		    xlator_t *this,
		    fd_t *fd,
		    size_t size,
		    off_t offset,
		    uint32_t flags, dict_t *xdata)
{
	int32_t ret;
	crypt_local_t *local;
	struct crypt_inode_info *info;
	struct gf_flock lock = {0, };

#if DEBUG_CRYPT
	gf_log("crypt", GF_LOG_DEBUG, "reading %d bytes from offset %llu",
	       (int)size, (long long)offset);
	if (parent_is_crypt_xlator(frame, this))
		gf_log("crypt", GF_LOG_DEBUG, "parent is crypt");
#endif	
	local = crypt_alloc_local(frame, this, GF_FOP_READ);
	if (!local) {
		ret = ENOMEM;
		goto error;
	}
	if (size == 0)
		goto trivial;

	local->fd = fd_ref(fd);
	local->flags = flags;

	info = local_get_inode_info(local, this);
	if (info == NULL) {
		ret = EINVAL;
		fd_unref(fd);
		goto error;
	}
	if (!object_alg_atomic(&info->cinfo)) {
		ret = EINVAL;
		fd_unref(fd);
		goto error;
	}
	set_config_offsets(frame, this, offset, size,
			   DATA_ATOM, 0);
	if (parent_is_crypt_xlator(frame, this)) {
		data_t *data;
		/*
		 * We are called by crypt_writev (or cypt_ftruncate)
		 * to perform the "read" component of the read-modify-write
		 * (or read-prune-write) sequence for some atom;
		 *
		 * don't ask for access:
		 * it has already been acquired
		 *
		 * Retrieve current file size
		 */
		if (!xdata) {
			gf_log("crypt", GF_LOG_WARNING,
			       "Regular file size hasn't been passed");
			ret = EIO;
			goto error;
		}
		data = dict_get(xdata, FSIZE_XATTR_PREFIX);
		if (!data) {
			gf_log("crypt", GF_LOG_WARNING,
			       "Regular file size not found");
			ret = EIO;
			goto error;
		}
		local->old_file_size =
			local->cur_file_size = data_to_uint64(data);

		get_one_call(frame);
		STACK_WIND(frame,
			   crypt_readv_cbk,
			   FIRST_CHILD(this),
			   FIRST_CHILD(this)->fops->readv,
			   local->fd,
			   /*
			    * FIXME: read amount can be reduced
			    */
			   local->data_conf.expanded_size,
			   local->data_conf.aligned_offset,
			   flags,
			   NULL);
		return 0;
	}
	if (xdata)
		local->xdata = dict_ref(xdata);

	lock.l_len    = 0;
        lock.l_start  = 0;
        lock.l_type   = F_RDLCK;
        lock.l_whence = SEEK_SET;

	STACK_WIND(frame,
		   crypt_readv_finodelk_cbk,
		   FIRST_CHILD(this),
		   FIRST_CHILD(this)->fops->finodelk,
		   this->name,
		   fd,
		   F_SETLKW,
		   &lock,
		   NULL);
	return 0;
 trivial:
	STACK_WIND(frame,
		   readv_trivial_completion,
		   FIRST_CHILD(this),
		   FIRST_CHILD(this)->fops->fstat,
		   fd,
		   NULL);
	return 0;
 error:
	STACK_UNWIND_STRICT(readv,
			    frame,
			    -1,
			    ret,
			    NULL,
			    0,
			    NULL,
			    NULL,
			    NULL);
	return 0;
}

void set_local_io_params_writev(call_frame_t *frame,
				struct object_cipher_info *object,
				struct rmw_atom *atom,
				off_t io_offset,
				uint32_t io_size)
{
	crypt_local_t *local = frame->local;

	local->io_offset = io_offset;
	local->io_size = io_size;

	local->io_offset_nopad =
		atom->offset_at(frame, object) + atom->offset_in(frame, object);

	gf_log("crypt", GF_LOG_DEBUG,
	       "set nopad offset to %llu",
	       (unsigned long long)local->io_offset_nopad);

	local->io_size_nopad = atom->io_size_nopad(frame, object);

	gf_log("crypt", GF_LOG_DEBUG,
	       "set nopad size to %llu",
	       (unsigned long long)local->io_size_nopad);

	local->update_disk_file_size = 0;
	/*
	 * NOTE: eof_padding_size is 0 for all full atoms;
	 * For head and tail atoms it will be set up at rmw_partial block()
	 */
	local->new_file_size = local->cur_file_size;

	if (local->io_offset_nopad + local->io_size_nopad > local->cur_file_size) {

		local->new_file_size = local->io_offset_nopad + local->io_size_nopad;

		gf_log("crypt", GF_LOG_DEBUG,
		       "set new file size to %llu",
		       (unsigned long long)local->new_file_size);

		local->update_disk_file_size = 1;
	}
}

void set_local_io_params_ftruncate(call_frame_t *frame,
				   struct object_cipher_info *object)
{
	uint32_t resid;
	crypt_local_t *local = frame->local;
	struct avec_config *conf = &local->data_conf;

	resid = conf->orig_offset & (object_alg_blksize(object) - 1);
	if (resid) {
		local->eof_padding_size =
			object_alg_blksize(object) - resid;
		local->new_file_size = conf->aligned_offset;
		local->update_disk_file_size = 0;
		/*
		 * file size will be updated
		 * in the ->writev() stack,
		 * when submitting file tail
		 */
	}
	else {	
		local->eof_padding_size = 0;
		local->new_file_size = conf->orig_offset;
		local->update_disk_file_size = 1;
		/*
		 * file size will be updated
		 * in this ->ftruncate stack
		 */
	}
}

static inline void submit_head(call_frame_t *frame, xlator_t *this)
{
	crypt_local_t *local = frame->local;
	submit_partial(frame, this, local->fd, HEAD_ATOM);
}

static inline void submit_tail(call_frame_t *frame, xlator_t *this)
{
	crypt_local_t *local = frame->local;
	submit_partial(frame, this, local->fd, TAIL_ATOM);
}

static void submit_hole(call_frame_t *frame, xlator_t *this)
{
	/*
	 * hole conversion always means
	 * appended write and goes in ordered fashion
	 */
	do_ordered_submit(frame, this, HOLE_ATOM);
}

static void submit_data(call_frame_t *frame, xlator_t *this)
{
	if (is_ordered_mode(frame)) {
		do_ordered_submit(frame, this, DATA_ATOM);
		return;
	}
	gf_log("crypt", GF_LOG_WARNING, "Bad submit mode");
	get_nr_calls(frame, nr_calls_data(frame));
	do_parallel_submit(frame, this, DATA_ATOM);
	return;
}

/*
 * heplers called by writev_cbk, fruncate_cbk in ordered mode
 */

static inline int32_t should_submit_hole(crypt_local_t *local)
{
	struct avec_config *conf = &local->hole_conf;

	return conf->avec != NULL;
}

static inline int32_t should_resume_submit_hole(crypt_local_t *local)
{
	struct avec_config *conf = &local->hole_conf;

	if (local->fop == GF_FOP_WRITE && has_tail_block(conf))
		/*
		 * Don't submit a part of hole, which
		 * fits into a data block:
 		 * this part of hole will be converted
		 * as a gap filled by zeros in data head
		 * block.
		 */
		return conf->cursor < conf->acount - 1;
	else
		return conf->cursor < conf->acount;
}

static inline int32_t should_resume_submit_data(call_frame_t *frame)
{
	crypt_local_t *local = frame->local;
	struct avec_config *conf = &local->data_conf;

	if (is_ordered_mode(frame))
		return conf->cursor < conf->acount;
	/*
	 * parallel writes
	 */
	return 0;
}

static inline int32_t should_submit_data_after_hole(crypt_local_t *local)
{
	return local->data_conf.avec != NULL;
}

static void update_local_file_params(call_frame_t *frame,
				     xlator_t *this,
				     struct iatt *prebuf,
				     struct iatt *postbuf)
{
	crypt_local_t *local = frame->local;

	check_buf(frame, this, postbuf);

	local->prebuf = *prebuf;
	local->postbuf = *postbuf;

	local->prebuf.ia_size  = local->cur_file_size;
	local->postbuf.ia_size = local->new_file_size;

	local->cur_file_size = local->new_file_size;	
}

static int32_t end_writeback_writev(call_frame_t *frame,
				    void *cookie,
				    xlator_t *this,
				    int32_t op_ret,
				    int32_t op_errno,
				    struct iatt *prebuf,
				    struct iatt *postbuf,
				    dict_t *xdata)
{
	crypt_local_t *local = frame->local;

	local->op_ret = op_ret;
	local->op_errno = op_errno;

	if (op_ret <= 0) {
		gf_log(this->name, GF_LOG_WARNING,
		       "writev iteration failed");
		goto put_one_call;
	}
	/*
	 * op_ret includes paddings (atom's head, atom's tail and EOF)
	 */
	if (op_ret < local->io_size) {
		gf_log(this->name, GF_LOG_WARNING,
		       "Incomplete writev iteration");
		goto put_one_call;
	}
	op_ret -= local->eof_padding_size;
	local->op_ret = op_ret;

	update_local_file_params(frame, this, prebuf, postbuf);

	if (data_write_in_progress(local)) {

		LOCK(&local->rw_count_lock);
		local->rw_count += op_ret;
		UNLOCK(&local->rw_count_lock);

		if (should_resume_submit_data(frame))
			submit_data(frame, this);
	}
	else {
		/*
		 * hole conversion is going on;
		 * don't take into account written zeros
		 */
		if (should_resume_submit_hole(local))
			submit_hole(frame, this);

		else if (should_submit_data_after_hole(local))
			submit_data(frame, this);
	}
 put_one_call:
	put_one_call_writev(frame, this);
	return 0;
}

#define crypt_writev_cbk end_writeback_writev

#define HOLE_WRITE_CHUNK_BITS 12
#define HOLE_WRITE_CHUNK_SIZE (1 << HOLE_WRITE_CHUNK_BITS)

/*
 * Convert hole of size @size at offset @off to
 * zeros and prepare respective iovecs for submit.
 * The hole lock should be held.
 *
 * Pre-conditions:
 * @local->file_size is set and valid.
 */
int32_t prepare_for_submit_hole(call_frame_t *frame, xlator_t *this,
				uint64_t off, off_t size)
{
	int32_t ret;
	crypt_local_t *local = frame->local;
	struct object_cipher_info *object = &local->info->cinfo;

	set_config_offsets(frame, this, off, size, HOLE_ATOM, 1);

	ret = set_config_avec_hole(this, local,
				   &local->hole_conf, object, local->fop);
	crypt_check_conf(&local->hole_conf);

	return ret;
}

/*
 * prepare for submit @count bytes at offset @from
 */
int32_t prepare_for_submit_data(call_frame_t *frame, xlator_t *this,
				off_t from, int32_t size, struct iovec *vec,
				int32_t vec_count, int32_t setup_gap)
{
	uint32_t ret;
	crypt_local_t *local = frame->local;
	struct object_cipher_info *object = &local->info->cinfo;

	set_config_offsets(frame, this, from, size,
			   DATA_ATOM, setup_gap);

	ret = set_config_avec_data(this, local,
				   &local->data_conf, object, vec, vec_count);
	crypt_check_conf(&local->data_conf);

	return ret;
}

static void free_avec(struct iovec *avec,
		      char **pool, int blocks_in_pool)
{
	if (!avec)
		return;
	GF_FREE(pool);
	GF_FREE(avec);
}

static void free_avec_data(crypt_local_t *local)
{
	return free_avec(local->data_conf.avec,
			 local->data_conf.pool,
			 local->data_conf.blocks_in_pool);
}

static void free_avec_hole(crypt_local_t *local)
{
	return free_avec(local->hole_conf.avec,
			 local->hole_conf.pool,
			 local->hole_conf.blocks_in_pool);
}


static void do_parallel_submit(call_frame_t *frame, xlator_t *this,
			       atom_data_type dtype)
{
	crypt_local_t *local = frame->local;
	struct avec_config *conf;

	local->active_setup = dtype;
	conf = conf_by_type(frame, dtype);

	if (has_head_block(conf))
		submit_head(frame, this);

	if (has_full_blocks(conf))
		submit_full(frame, this);

	if (has_tail_block(conf))
		submit_tail(frame, this);
	return;
}

static void do_ordered_submit(call_frame_t *frame, xlator_t *this,
			      atom_data_type dtype)
{
	crypt_local_t *local = frame->local;
	struct avec_config *conf;

	local->active_setup = dtype;
	conf = conf_by_type(frame, dtype);

	if (should_submit_head_block(conf)) {
		get_one_call_nolock(frame);
		submit_head(frame, this);
	}
	else if (should_submit_full_block(conf)) {
		get_one_call_nolock(frame);
		submit_full(frame, this);
	}
	else if (should_submit_tail_block(conf)) {
		get_one_call_nolock(frame);
		submit_tail(frame, this);
	}
	else
		gf_log("crypt", GF_LOG_DEBUG,
		       "nothing has been submitted in ordered mode");
	return;
}

static int32_t do_writev(call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno,
			 dict_t *dict,
			 dict_t *xdata)
{
	data_t *data;
	crypt_local_t *local = frame->local;
	struct object_cipher_info *object = &local->info->cinfo;
	/*
	 * extract regular file size
	 */
	data = dict_get(dict, FSIZE_XATTR_PREFIX);
	if (!data) {
		gf_log("crypt", GF_LOG_WARNING, "Regular file size not found");
		op_ret = -1;
		op_errno = EIO;
		goto error;
	}
	local->old_file_size = local->cur_file_size = data_to_uint64(data);

	set_gap_at_end(frame, object, &local->data_conf, DATA_ATOM);

	if (local->cur_file_size < local->data_conf.orig_offset) {
		/*
		 * Set up hole config
		 */
		op_errno = prepare_for_submit_hole(frame,
			       this,
			       local->cur_file_size,
			       local->data_conf.orig_offset - local->cur_file_size);
		if (op_errno) {
			local->op_ret = -1;
			local->op_errno = op_errno;
			goto error;
		}
	}
	if (should_submit_hole(local))
		submit_hole(frame, this);
	else 
		submit_data(frame, this);
	return 0;
 error:
	get_one_call_nolock(frame);
	put_one_call_writev(frame, this);
	return 0;
}

static int32_t crypt_writev_finodelk_cbk(call_frame_t *frame,
					 void *cookie,
					 xlator_t *this,
					 int32_t op_ret,
					 int32_t op_errno,
					 dict_t *xdata)
{
	crypt_local_t *local = frame->local;

	local->op_ret = op_ret;
	local->op_errno = op_errno;

	if (op_ret < 0)
		goto error;
	/*
	 * An access has been granted,
	 * retrieve file size first
	 */
	STACK_WIND(frame,
		   do_writev,
		   FIRST_CHILD(this),
		   FIRST_CHILD(this)->fops->fgetxattr,
		   local->fd,
		   FSIZE_XATTR_PREFIX,
		   NULL);
	return 0;
 error:
	get_one_call_nolock(frame);
	put_one_call_writev(frame, this);
	return 0;	
}

static int32_t writev_trivial_completion(call_frame_t *frame,
					 void *cookie,
					 xlator_t *this,
					 int32_t op_ret,
					 int32_t op_errno,
					 struct iatt *buf,
					 dict_t *dict)
{
	crypt_local_t *local = frame->local;
	
	local->op_ret = op_ret;
	local->op_errno = op_errno;
	local->prebuf = *buf;
	local->postbuf = *buf;

	local->prebuf.ia_size = local->cur_file_size;
	local->postbuf.ia_size = local->cur_file_size;

	get_one_call(frame);
	put_one_call_writev(frame, this);
	return 0;
}

int crypt_writev(call_frame_t *frame,
		 xlator_t *this,
		 fd_t *fd,
		 struct iovec *vec,
		 int32_t count,
		 off_t offset,
		 uint32_t flags,
		 struct iobref *iobref,
		 dict_t *xdata)
{
	int32_t ret;
	crypt_local_t *local;
	struct crypt_inode_info *info;
	struct gf_flock lock = {0, };
#if DEBUG_CRYPT
	gf_log ("crypt", GF_LOG_DEBUG, "writing %d bytes from offset %llu",
		(int)iovec_get_size(vec, count), (long long)offset);
#endif
	local = crypt_alloc_local(frame, this, GF_FOP_WRITE);
	if (!local) {
		ret = ENOMEM;
		goto error;
	}
	local->fd = fd_ref(fd);

	if (iobref)
		local->iobref = iobref_ref(iobref);
	/*
	 * to update real file size on the server
	 */
	local->xattr = dict_new();
	if (!local->xattr) {
		ret = ENOMEM;
		goto error;
	}
	local->flags = flags;

	info = local_get_inode_info(local, this);
	if (info == NULL) {
		ret = EINVAL;
		goto error;
	}
	if (!object_alg_atomic(&info->cinfo)) {
		ret = EINVAL;
		goto error;
	}
	if (iovec_get_size(vec, count) == 0)
		goto trivial;

	ret = prepare_for_submit_data(frame, this, offset,
				      iovec_get_size(vec, count),
				      vec, count, 0 /* don't setup gup
						       in tail: we don't
						       know file size yet */);
	if (ret)
		goto error;

	if (parent_is_crypt_xlator(frame, this)) {
		data_t *data;
		/*
		 * we are called by shinking crypt_ftruncate(),
		 * which doesn't perform hole conversion;
		 *
		 * don't ask for access:
		 * it has already been acquired
		 */

		/*
		 * extract file size
		 */
		if (!xdata) {
			gf_log("crypt", GF_LOG_WARNING,
			       "Regular file size hasn't been passed");
			ret = EIO;
			goto error;
		}
		data = dict_get(xdata, FSIZE_XATTR_PREFIX);
		if (!data) {
			gf_log("crypt", GF_LOG_WARNING,
			       "Regular file size not found");
			ret = EIO;
			goto error;
		}
		local->old_file_size =
			local->cur_file_size = data_to_uint64(data);

		submit_data(frame, this);
		return 0;
	}
	if (xdata)
		local->xdata = dict_ref(xdata);
	/*
	 * lock the file and retrieve its size
	 */
	lock.l_len    = 0;
        lock.l_start  = 0;
        lock.l_type   = F_WRLCK;
        lock.l_whence = SEEK_SET;

	STACK_WIND(frame,
		   crypt_writev_finodelk_cbk,
		   FIRST_CHILD(this),
		   FIRST_CHILD(this)->fops->finodelk,
		   this->name,
		   fd,
		   F_SETLKW,
		   &lock,
		   NULL);
	return 0;
 trivial:
	STACK_WIND(frame,
		   writev_trivial_completion,
		   FIRST_CHILD(this),
		   FIRST_CHILD(this)->fops->fstat,
		   fd,
		   NULL);
	return 0;
 error:
	if (local && local->fd)
		fd_unref(fd);
	if (local && local->iobref)
		iobref_unref(iobref);
	if (local && local->xdata)
		dict_unref(xdata);
	if (local && local->xattr)
		dict_unref(local->xattr);
	if (local && local->info)
		free_inode_info(local->info);

	STACK_UNWIND_STRICT(writev, frame, -1, ret, NULL, NULL, NULL);
	return 0;
}

int32_t prepare_for_prune(call_frame_t *frame, xlator_t *this, uint64_t offset)
{
	set_config_offsets(frame, this,
			   offset,
			   0, /* count */
			   DATA_ATOM,
			   0 /* since we prune, there is no
				gap in tail to uptodate */);
	return 0;
}

/*
 * Finish the read-prune-modify sequence
 *
 * Can be invoked as
 * 1) ->ftruncate_cbk() for cblock-aligned, or trivial prune
 * 2) ->writev_cbk() for non-cblock-aligned prune
 */

static int32_t prune_complete(call_frame_t *frame,
			      void *cookie,
			      xlator_t *this,
			      int32_t op_ret,
			      int32_t op_errno,
			      struct iatt *prebuf,
			      struct iatt *postbuf,
			      dict_t *xdata)
{
	crypt_local_t *local = frame->local;

	local->op_ret = op_ret;
	local->op_errno = op_errno;

	update_local_file_params(frame, this, prebuf, postbuf);

	put_one_call_ftruncate(frame, this);
	return 0;
}

/*
 * This is called as ->ftruncate_cbk()
 *
 * Perform the "write" component of the
 * read-prune-write sequence.
 *
 * submuit the rest of the file
 */
static int32_t prune_submit_file_tail(call_frame_t *frame,
				      void *cookie,
				      xlator_t *this,
				      int32_t op_ret,
				      int32_t op_errno,
				      struct iatt *prebuf,
				      struct iatt *postbuf,
				      dict_t *xdata)
{
	crypt_local_t *local = frame->local;
	struct avec_config *conf = &local->data_conf;
	dict_t *dict;

	if (op_ret < 0)
		goto put_one_call;

	if (local->xdata) {
		dict_unref(local->xdata);
		local->xdata = NULL;
	}
	if (xdata)
		local->xdata = dict_ref(xdata);

	dict = dict_new();
	if (!dict) {
		op_errno = ENOMEM;
		goto error;
	}

	update_local_file_params(frame, this, prebuf, postbuf);
	local->new_file_size = conf->orig_offset;

	/*
	 * The rest of the file is a partial block and, hence,
	 * should be written via RMW sequence, so the crypt xlator
	 * does STACK_WIND to itself.
	 *
	 * Pass current file size to crypt_writev()
	 */
	op_errno = dict_set(dict,
			    FSIZE_XATTR_PREFIX,
			    data_from_uint64(local->cur_file_size));
	if (op_errno) {
		gf_log("crypt", GF_LOG_WARNING,
		       "can not set key to update file size");
		dict_unref(dict);
		goto error;
	}
	gf_log("crypt", GF_LOG_DEBUG,
	       "passing current file size (%llu) to crypt_writev",
	       (unsigned long long)local->cur_file_size);
	/*
	 * Padding will be filled with
	 * zeros by rmw_partial_block()
	 */
	STACK_WIND(frame,
		   prune_complete,
		   this,
		   this->fops->writev, /* crypt_writev */
		   local->fd,
		   &local->vec,
		   1,
		   conf->aligned_offset, /* offset to write from */
		   0,
		   local->iobref,
		   dict);

	dict_unref(dict);
	return 0;
 error:
	local->op_ret = -1;
	local->op_errno = op_errno;
 put_one_call:
	put_one_call_ftruncate(frame, this);
	return 0;
}

/*
 * This is called as a callback of ->writev() invoked in behalf
 * of ftruncate(): it can be
 * 1) ordered writes issued by hole conversion in the case of
 *    expanded truncate, or
 * 2) an rmw partial data block issued by non-cblock-aligned
 * prune.
 */
int32_t end_writeback_ftruncate(call_frame_t *frame,
				void *cookie,
				xlator_t *this,
				int32_t op_ret,
				int32_t op_errno,
				struct iatt *prebuf,
				struct iatt *postbuf,
				dict_t *xdata)
{
	crypt_local_t  *local = frame->local;
	/*
	 * if nothing has been written,
	 * then it must be an error
	 */
	local->op_ret = op_ret;
	local->op_errno = op_errno;

	if (op_ret < 0)
		goto put_one_call;

	update_local_file_params(frame, this, prebuf, postbuf);

	if (data_write_in_progress(local))
		/* case (2) */
		goto put_one_call;
	/* case (1) */
	if (should_resume_submit_hole(local))
		submit_hole(frame, this);
	/*
	 * case of hole, when we should't resume
	 */
 put_one_call:
	put_one_call_ftruncate(frame, this);
	return 0;
}

/*
 * Perform prune and write components of the
 * read-prune-write sequence.
 *
 * Called as ->readv_cbk()
 *
 * Pre-conditions:
 * @vec contains the latest atom of the file
 * (plain text)
 */
static int32_t prune_write(call_frame_t *frame,
			   void *cookie,
			   xlator_t *this,
			   int32_t op_ret,
			   int32_t op_errno,
			   struct iovec *vec,
			   int32_t count,
			   struct iatt *stbuf,
			   struct iobref *iobref,
			   dict_t *xdata)
{
	int32_t i;
	size_t to_copy;
	size_t copied = 0;
	crypt_local_t *local = frame->local;
	struct avec_config *conf = &local->data_conf;

	local->op_ret = op_ret;
	local->op_errno = op_errno;
	if (op_ret == -1)
		goto put_one_call;

	/*
	 * At first, uptodate head block
	 */
	if (iovec_get_size(vec, count) < conf->off_in_head) {
		gf_log(this->name, GF_LOG_WARNING,
		       "Failed to uptodate head block for prune");
		local->op_ret = -1;
		local->op_errno = EIO;
		goto put_one_call;
	}
	local->vec.iov_len = conf->off_in_head;
	local->vec.iov_base = GF_CALLOC(1, local->vec.iov_len,
					gf_crypt_mt_data);

	if (local->vec.iov_base == NULL) {
	        gf_log(this->name, GF_LOG_WARNING,
                       "Failed to calloc head block for prune");
		local->op_ret = -1;
		local->op_errno = ENOMEM;
	        goto put_one_call;
	}
	for (i = 0; i < count; i++) {
		to_copy = vec[i].iov_len;
		if (to_copy > local->vec.iov_len - copied)
			to_copy = local->vec.iov_len - copied;

		memcpy((char *)local->vec.iov_base + copied,
		       vec[i].iov_base,
		       to_copy);
		copied += to_copy;
		if (copied == local->vec.iov_len)
			break;
	}
	/*
	 * perform prune with aligned offset
	 * (i.e. at this step we prune a bit
	 * more then it is needed
	 */
	STACK_WIND(frame,
		   prune_submit_file_tail,
		   FIRST_CHILD(this),
		   FIRST_CHILD(this)->fops->ftruncate,
		   local->fd,
		   conf->aligned_offset,
		   local->xdata);
	return 0;
 put_one_call:
	put_one_call_ftruncate(frame, this);
	return 0;
}

/*
 * Perform a read-prune-write sequence
 */
int32_t read_prune_write(call_frame_t *frame, xlator_t *this)
{
	int32_t ret = 0;
	dict_t *dict = NULL;
	crypt_local_t  *local = frame->local;
	struct avec_config *conf = &local->data_conf;
	struct object_cipher_info *object = &local->info->cinfo;

	set_local_io_params_ftruncate(frame, object);
	get_one_call_nolock(frame);

	if ((conf->orig_offset & (object_alg_blksize(object) - 1)) == 0) {
		/*
		 * cblock-aligned prune:
		 * we don't need read and write components,
		 * just cut file body
		 */
		gf_log("crypt", GF_LOG_DEBUG,
		       "prune without RMW (at offset %llu",
		       (unsigned long long)conf->orig_offset);

		STACK_WIND(frame,
			   prune_complete,
			   FIRST_CHILD(this),
			   FIRST_CHILD(this)->fops->ftruncate,
			   local->fd,
			   conf->orig_offset,
			   local->xdata);
		return 0;
	}
	gf_log("crypt", GF_LOG_DEBUG,
	       "prune with RMW (at offset %llu",
	       (unsigned long long)conf->orig_offset);	
	/*
	 * We are about to perform the "read" component of the
	 * read-prune-write sequence. It means that we need to
	 * read encrypted data from disk and decrypt it.
	 * So, the crypt translator does STACK_WIND to itself.
	 * 
	 * Pass current file size to crypt_readv()
	 
	 */
	dict = dict_new();
	if (!dict) {
		gf_log("crypt", GF_LOG_WARNING, "Can not alloc dict");
		ret = ENOMEM;
		goto exit;
	}
	ret = dict_set(dict,
		       FSIZE_XATTR_PREFIX,
		       data_from_uint64(local->cur_file_size));
	if (ret) {
		gf_log("crypt", GF_LOG_WARNING, "Can not set dict");
		goto exit;
	}
	STACK_WIND(frame,
		   prune_write,
		   this,
		   this->fops->readv, /* crypt_readv */
		   local->fd,
		   get_atom_size(object), /* bytes to read */
		   conf->aligned_offset, /* offset to read from */
		   0,
		   dict);
 exit:
	if (dict)
		dict_unref(dict);
	return ret;
}

/*
 * File prune is more complicated than expand.
 * First we need to read the latest atom to not lose info
 * needed for proper update. Also we need to make sure that
 * every component of read-prune-write sequence leaves data
 * consistent
 *
 * Non-cblock aligned prune is performed as read-prune-write
 * sequence:
 *
 * 1) read the latest atom;
 * 2) perform cblock-aligned prune
 * 3) issue a write request for the end-of-file
 */
int32_t prune_file(call_frame_t *frame, xlator_t *this, uint64_t offset)
{
	int32_t ret;

	ret = prepare_for_prune(frame, this, offset);
	if (ret)
		return ret;
	return read_prune_write(frame, this);
}

int32_t expand_file(call_frame_t *frame, xlator_t *this,
		    uint64_t offset)
{
	int32_t ret;
	crypt_local_t *local = frame->local;

	ret = prepare_for_submit_hole(frame, this,
				      local->old_file_size,
				      offset - local->old_file_size);
	if (ret)
		return ret;
	submit_hole(frame, this);
	return 0;
}

static int32_t ftruncate_trivial_completion(call_frame_t *frame,
					    void *cookie,
					    xlator_t *this,
					    int32_t op_ret,
					    int32_t op_errno,
					    struct iatt *buf,
					    dict_t *dict)
{
	crypt_local_t *local = frame->local;

	local->op_ret = op_ret;
	local->op_errno = op_errno;
	local->prebuf = *buf;
	local->postbuf = *buf;

	local->prebuf.ia_size = local->cur_file_size;
	local->postbuf.ia_size = local->cur_file_size;

	get_one_call(frame);
	put_one_call_ftruncate(frame, this);
	return 0;
}

static int32_t do_ftruncate(call_frame_t *frame,
			    void *cookie,
			    xlator_t *this,
			    int32_t op_ret,
			    int32_t op_errno,
			    dict_t *dict,
			    dict_t *xdata)
{
	data_t *data;
	crypt_local_t *local = frame->local;

	if (op_ret)
		goto error;	
	/*
	 * extract regular file size
	 */
	data = dict_get(dict, FSIZE_XATTR_PREFIX);
	if (!data) {
		gf_log("crypt", GF_LOG_WARNING, "Regular file size not found");
		op_errno = EIO;
		goto error;
	}
	local->old_file_size = local->cur_file_size = data_to_uint64(data);

	if (local->data_conf.orig_offset == local->cur_file_size) {
#if DEBUG_CRYPT
		gf_log("crypt", GF_LOG_DEBUG,
		       "trivial ftruncate (current file size %llu)",
		       (unsigned long long)local->cur_file_size);
#endif
		goto trivial;
	}
	else if (local->data_conf.orig_offset < local->cur_file_size) {
#if DEBUG_CRYPT
		gf_log("crypt", GF_LOG_DEBUG, "prune from %llu to %llu",
		       (unsigned long long)local->cur_file_size,
		       (unsigned long long)local->data_conf.orig_offset);
#endif
		op_errno = prune_file(frame,
				      this,
				      local->data_conf.orig_offset);
	}
	else {
#if DEBUG_CRYPT
		gf_log("crypt", GF_LOG_DEBUG, "expand from %llu to %llu",
		       (unsigned long long)local->cur_file_size,
		       (unsigned long long)local->data_conf.orig_offset);
#endif
		op_errno = expand_file(frame,
				       this,
				       local->data_conf.orig_offset);
	}
	if (op_errno)
		goto error;
	return 0;
 trivial:
	STACK_WIND(frame,
		   ftruncate_trivial_completion,
		   FIRST_CHILD(this),
		   FIRST_CHILD(this)->fops->fstat,
		   local->fd,
		   NULL);
	return 0;
 error:
	/*
	 * finish with ftruncate
	 */
	local->op_ret = -1;
	local->op_errno = op_errno;

	get_one_call_nolock(frame);
	put_one_call_ftruncate(frame, this);
	return 0;
}

static int32_t crypt_ftruncate_finodelk_cbk(call_frame_t *frame,
					    void *cookie,
					    xlator_t *this,
					    int32_t op_ret,
					    int32_t op_errno,
					    dict_t *xdata)
{
	crypt_local_t *local = frame->local;

	local->op_ret = op_ret;
	local->op_errno = op_errno;

	if (op_ret < 0)
		goto error;
	/*
	 * An access has been granted,
	 * retrieve file size first
	 */
	STACK_WIND(frame,
		   do_ftruncate,
		   FIRST_CHILD(this),
		   FIRST_CHILD(this)->fops->fgetxattr,
		   local->fd,
		   FSIZE_XATTR_PREFIX,
		   NULL);
	return 0;
 error:
	get_one_call_nolock(frame);
	put_one_call_ftruncate(frame, this);
	return 0;
}

/*
 * ftruncate is performed in 2 steps:
 * . recieve file size;
 * . expand or prune file.
 */
static int32_t crypt_ftruncate(call_frame_t *frame,
			       xlator_t *this,
			       fd_t *fd,
			       off_t offset,
			       dict_t *xdata)
{
	int32_t ret;
	crypt_local_t *local;
	struct crypt_inode_info *info;
	struct gf_flock lock = {0, };

	local = crypt_alloc_local(frame, this, GF_FOP_FTRUNCATE);
	if (!local) {
		ret = ENOMEM;
		goto error;
	}
	local->xattr = dict_new();
	if (!local->xattr) {
		ret = ENOMEM;
		goto error;
	}
	local->fd = fd_ref(fd);
	info = local_get_inode_info(local, this);
	if (info == NULL) {
		ret = EINVAL;
		goto error;
	}
	if (!object_alg_atomic(&info->cinfo)) {
		ret = EINVAL;
		goto error;
	}
	local->data_conf.orig_offset = offset;
	if (xdata)
		local->xdata = dict_ref(xdata);

	lock.l_len    = 0;
        lock.l_start  = 0;
        lock.l_type   = F_WRLCK;
        lock.l_whence = SEEK_SET;

	STACK_WIND(frame,
		   crypt_ftruncate_finodelk_cbk,
		   FIRST_CHILD(this),
		   FIRST_CHILD(this)->fops->finodelk,
		   this->name,
		   fd,
		   F_SETLKW,
		   &lock,
		   NULL);
	return 0;
 error:
	if (local && local->fd)
		fd_unref(fd);
	if (local && local->xdata)
		dict_unref(xdata);
	if (local && local->xattr)
		dict_unref(local->xattr);
	if (local && local->info)
		free_inode_info(local->info);

	STACK_UNWIND_STRICT(ftruncate, frame, -1, ret, NULL, NULL, NULL);
	return 0;
}

/* ->flush_cbk() */
int32_t truncate_end(call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     dict_t *xdata)
{
	crypt_local_t *local = frame->local;

	STACK_UNWIND_STRICT(truncate,
			    frame,
			    op_ret,
			    op_errno,
			    &local->prebuf,
			    &local->postbuf,
			    local->xdata);
	return 0;
}

/* ftruncate_cbk() */
int32_t truncate_flush(call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno,
		       struct iatt *prebuf,
		       struct iatt *postbuf,
		       dict_t *xdata)
{
	crypt_local_t *local = frame->local;
	fd_t *fd = local->fd;
	local->prebuf = *prebuf;
	local->postbuf = *postbuf;

	STACK_WIND(frame,
		   truncate_end,
		   FIRST_CHILD(this),
		   FIRST_CHILD(this)->fops->flush,
		   fd,
		   NULL);
	fd_unref(fd);
	return 0;
}

/*
 * is called as ->open_cbk()
 */
static int32_t truncate_begin(call_frame_t *frame,
			      void *cookie,
			      xlator_t *this,
			      int32_t op_ret,
			      int32_t op_errno,
			      fd_t *fd,
			      dict_t *xdata)
{
	crypt_local_t *local = frame->local;

	if (op_ret < 0) {
		fd_unref(fd);
		STACK_UNWIND_STRICT(truncate,
				    frame,
				    op_ret,
				    op_errno, NULL, NULL, NULL);
		return 0;
	}
	/*
	 * crypt_truncate() is implemented via crypt_ftruncate(),
	 * so the crypt xlator does STACK_WIND to itself here
	 */
	STACK_WIND(frame,
		   truncate_flush,
		   this,
		   this->fops->ftruncate, /* crypt_ftruncate */
		   fd,
		   local->offset,
		   NULL);
	return 0;
}

/*
 * crypt_truncate() is implemented via crypt_ftruncate() as a
 * sequence crypt_open() - crypt_ftruncate() - truncate_flush()
 */
int32_t crypt_truncate(call_frame_t *frame,
		       xlator_t *this,
		       loc_t *loc,
		       off_t offset,
		       dict_t *xdata)
{
	fd_t *fd;
	crypt_local_t *local;

#if DEBUG_CRYPT
	gf_log(this->name, GF_LOG_DEBUG,
	       "truncate file %s at offset %llu",
	       loc->path, (unsigned long long)offset);
#endif
	local = crypt_alloc_local(frame, this, GF_FOP_TRUNCATE);
	if (!local)
		goto error;

	fd = fd_create(loc->inode, frame->root->pid);
	if (!fd) {
		gf_log(this->name, GF_LOG_ERROR, "Can not create fd");
		goto error;
	}
	local->fd = fd;
	local->offset = offset;
	local->xdata = xdata;
	STACK_WIND(frame,
		   truncate_begin,
		   this,
		   this->fops->open, /* crypt_open() */
		   loc,
		   O_RDWR,
		   fd,
		   NULL);
	return 0;
 error:
	STACK_UNWIND_STRICT(truncate, frame, -1, EINVAL, NULL, NULL, NULL);
	return 0;
}

end_writeback_handler_t dispatch_end_writeback(glusterfs_fop_t fop)
{
	switch (fop) {
	case GF_FOP_WRITE:
		return end_writeback_writev;
	case GF_FOP_FTRUNCATE:
		return end_writeback_ftruncate;
	default:
		gf_log("crypt", GF_LOG_WARNING, "Bad wb operation %d", fop);
		return NULL;
	}
}

/*
 * true, if the caller needs metadata string
 */
static int32_t is_custom_mtd(dict_t *xdata)
{
	data_t *data;
	uint32_t flags;

	if (!xdata)
		return 0;

	data = dict_get(xdata, MSGFLAGS_PREFIX);
	if (!data)
		return 0;
	if (data->len != sizeof(uint32_t)) {
		gf_log("crypt", GF_LOG_WARNING,
		       "Bad msgflags size (%d)", data->len);
		return -1;
	}
	flags = *((uint32_t *)data->data);
	return msgflags_check_mtd_lock(&flags);
}

static int32_t crypt_open_done(call_frame_t *frame,
			       void *cookie,
			       xlator_t *this,
			       int32_t op_ret,
			       int32_t op_errno, dict_t *xdata)
{
	crypt_local_t *local = frame->local;

	local->op_ret = op_ret;
	local->op_errno = op_errno;
	if (op_ret < 0)
		gf_log(this->name, GF_LOG_WARNING, "mtd unlock failed (%d)",
		       op_errno);
	put_one_call_open(frame);
	return 0;
}

static void crypt_open_tail(call_frame_t *frame, xlator_t *this)
{
	struct gf_flock  lock  = {0, };
	crypt_local_t *local = frame->local;

        lock.l_type   = F_UNLCK;
        lock.l_whence = SEEK_SET;
        lock.l_start  = 0;
        lock.l_len    = 0;
        lock.l_pid    = 0;

	STACK_WIND(frame,
		   crypt_open_done,
		   FIRST_CHILD(this),
		   FIRST_CHILD(this)->fops->finodelk,
		   this->name,
		   local->fd,
		   F_SETLKW,
		   &lock,
		   NULL);
}

/*
 * load private inode info at open time
 * called as ->fgetxattr_cbk()
 */
static int load_mtd_open(call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno,
			 dict_t *dict,
			 dict_t *xdata)
{
	int32_t ret;
	gf_boolean_t upload_info;
	data_t *mtd;
	uint64_t value = 0;
	struct crypt_inode_info *info;
	crypt_local_t *local = frame->local;
	crypt_private_t *priv = this->private;

	local->op_ret = op_ret;
	local->op_errno = op_errno;

	if (local->fd->inode->ia_type == IA_IFLNK)
		goto exit;
	if (op_ret < 0)
		goto exit;
	/*
	 * first, check for cached info
	 */
	ret = inode_ctx_get(local->fd->inode, this, &value);
	if (ret != -1) {
		info = (struct crypt_inode_info *)(long)value;
		if (info == NULL) {
			gf_log(this->name, GF_LOG_WARNING,
			       "Inode info expected, but not found");
			local->op_ret = -1;
			local->op_errno = EIO;
			goto exit;
		}
		/*
		 * info has been found in the cache
		 */
		upload_info = _gf_false;
	}
	else {
		/*
		 * info hasn't been found in the cache.
		 */
		info = alloc_inode_info(local, local->loc);
		if (!info) {
			local->op_ret = -1;
			local->op_errno = ENOMEM;
			goto exit;
		}
		init_inode_info_head(info, local->fd);
		upload_info = _gf_true;
	}
	/*
	 * extract metadata
	 */
	mtd = dict_get(dict, CRYPTO_FORMAT_PREFIX);
	if (!mtd) {
		local->op_ret = -1;
		local->op_errno = ENOENT;
		gf_log (this->name, GF_LOG_WARNING,
			"Format string wasn't found");
		goto exit;
	}
	/*
	 * authenticate metadata against the path
	 */	
	ret = open_format((unsigned char *)mtd->data,
			  mtd->len,
			  local->loc,
			  info,
			  get_master_cinfo(priv),
			  local,
			  upload_info);
	if (ret) {
		local->op_ret = -1;
		local->op_errno = ret;
		goto exit;
	}
	if (upload_info) {
		ret = init_inode_info_tail(info, get_master_cinfo(priv));
		if (ret) {
			local->op_ret = -1;
			local->op_errno = ret;
			goto exit;	
		}
		ret = inode_ctx_put(local->fd->inode,
				    this, (uint64_t)(long)info);
		if (ret == -1) {
			local->op_ret = -1;
			local->op_errno = EIO;
			goto exit;
		}
	}
	if (local->custom_mtd) {
		/*
		 * pass the metadata string to the customer
		 */
		ret = dict_set_static_bin(local->xdata,
					  CRYPTO_FORMAT_PREFIX,
					  mtd->data,
					  mtd->len);
		if (ret) {
			local->op_ret = -1;
			local->op_errno = ret;
			goto exit;
		}
	}
 exit:
	if (!local->custom_mtd)
		crypt_open_tail(frame, this);
	else
		put_one_call_open(frame);
	return 0;
}

static int32_t crypt_open_finodelk_cbk(call_frame_t *frame,
				       void *cookie,
				       xlator_t *this,
				       int32_t op_ret,
				       int32_t op_errno,
				       dict_t *xdata)
{
	crypt_local_t *local = frame->local;

	local->op_ret = op_ret;
	local->op_errno = op_errno;

	if (op_ret < 0) {
		gf_log(this->name, GF_LOG_WARNING, "finodelk (LOCK) failed");
		goto exit;
	}
	STACK_WIND(frame,
		   load_mtd_open,
		   FIRST_CHILD(this),
		   FIRST_CHILD(this)->fops->fgetxattr,
		   local->fd,
		   CRYPTO_FORMAT_PREFIX,
		   NULL);
	return 0;
 exit:
	put_one_call_open(frame);
	return 0;
}

/*
 * verify metadata against the specified pathname
 */
static int32_t crypt_open_cbk(call_frame_t *frame,
			      void *cookie,
			      xlator_t *this,
			      int32_t op_ret,
			      int32_t op_errno,
			      fd_t *fd,
			      dict_t *xdata)
{
	struct gf_flock lock = {0, };
	crypt_local_t *local = frame->local;

	local->op_ret = op_ret;
	local->op_errno = op_errno;

	if (local->fd->inode->ia_type == IA_IFLNK)
		goto exit;
	if (op_ret < 0)
		goto exit;
	if (xdata)
		local->xdata = dict_ref(xdata);
	else if (local->custom_mtd){
		local->xdata = dict_new();
		if (!local->xdata) {
			local->op_ret = -1;
			local->op_errno = ENOMEM;
			gf_log ("crypt", GF_LOG_ERROR,
				"Can not get new dict for mtd string");
			goto exit;
		}
	}
	lock.l_len    = 0;
        lock.l_start  = 0;
        lock.l_type   = local->custom_mtd ? F_WRLCK : F_RDLCK;
        lock.l_whence = SEEK_SET;

	STACK_WIND(frame,
		   crypt_open_finodelk_cbk,
		   FIRST_CHILD(this),
		   FIRST_CHILD(this)->fops->finodelk,
		   this->name,
		   fd,
		   F_SETLKW,
		   &lock,
		   NULL);
	return 0;
 exit:
	put_one_call_open(frame);
	return 0;
}

static int32_t crypt_open(call_frame_t *frame,
			  xlator_t *this,
			  loc_t *loc,
			  int32_t flags,
			  fd_t *fd,
			  dict_t *xdata)
{
	int32_t ret = ENOMEM;
	crypt_local_t *local;

	local = crypt_alloc_local(frame, this, GF_FOP_OPEN);
	if (!local)
		goto error;
	local->loc = GF_CALLOC(1, sizeof(*loc), gf_crypt_mt_loc);
	if (!local->loc) {
		ret = ENOMEM;
		goto error;
	}
	memset(local->loc, 0, sizeof(*local->loc));
	ret = loc_copy(local->loc, loc);
	if (ret) {
		GF_FREE(local->loc);
		goto error;
	}
	local->fd = fd_ref(fd);

	ret = is_custom_mtd(xdata);
	if (ret < 0) {
		loc_wipe(local->loc);
		GF_FREE(local->loc);
		ret = EINVAL;
		goto error;
	}
	local->custom_mtd = ret;

	if ((flags & O_ACCMODE) == O_WRONLY)
		/*
		 * we can't open O_WRONLY, because
		 * we need to do read-modify-write
		 */
		flags = (flags & ~O_ACCMODE) | O_RDWR;
	/*
	 * Make sure that out translated offsets
	 * and counts won't be ignored
	 */
	flags &= ~O_APPEND;
	get_one_call_nolock(frame);
	STACK_WIND(frame,
		   crypt_open_cbk,
		   FIRST_CHILD(this),
		   FIRST_CHILD(this)->fops->open,
		   loc,
		   flags,
		   fd,
		   xdata);
	return 0;
 error:
	STACK_UNWIND_STRICT(open,
			    frame,
			    -1,
			    ret,
			    NULL,
			    NULL);
	return 0;
}

static int32_t init_inode_info_tail(struct crypt_inode_info *info,
				    struct master_cipher_info *master)
{
	int32_t ret;
	struct object_cipher_info *object = &info->cinfo;

#if DEBUG_CRYPT
	gf_log("crypt", GF_LOG_DEBUG, "Init inode info for object %s",
	       uuid_utoa(info->oid));
#endif
	ret = data_cipher_algs[object->o_alg][object->o_mode].set_private(info,
									master);
	if (ret) {
		gf_log("crypt", GF_LOG_ERROR, "Set private info failed");
		return ret;
	}
	return 0;
}

/*
 * Init inode info at ->create() time
 */
static void init_inode_info_create(struct crypt_inode_info *info,
				   struct master_cipher_info *master,
				   data_t *data)
{
	struct object_cipher_info *object;

	info->nr_minor = CRYPT_XLATOR_ID;
	memcpy(info->oid, data->data, data->len);

	object = &info->cinfo;

	object->o_alg        = master->m_alg;
	object->o_mode       = master->m_mode;
	object->o_block_bits = master->m_block_bits;
	object->o_dkey_size  = master->m_dkey_size;
}

static void init_inode_info_head(struct crypt_inode_info *info, fd_t *fd)
{
	memcpy(info->oid, fd->inode->gfid, sizeof(uuid_t));
}

static int32_t crypt_create_done(call_frame_t *frame,
				 void *cookie,
				 xlator_t *this,
				 int32_t op_ret,
				 int32_t op_errno, dict_t *xdata)
{
	crypt_private_t *priv = this->private;
	crypt_local_t *local = frame->local;
	struct crypt_inode_info *info = local->info;
	fd_t *local_fd = local->fd;
	dict_t *local_xdata = local->xdata;
	inode_t *local_inode = local->inode;

	if (op_ret < 0) {
		free_inode_info(info);
		goto unwind;
	}
	op_errno = init_inode_info_tail(info, get_master_cinfo(priv));
	if (op_errno) {
		op_ret = -1;
		free_inode_info(info);
		goto unwind;
	}
	/*
	 * FIXME: drop major subversion number
	 */
	op_ret = inode_ctx_put(local->fd->inode, this, (uint64_t)(long)info);
	if (op_ret == -1) {
		op_errno = EIO;
		free_inode_info(info);
		goto unwind;
	}
 unwind:
	free_format(local);
	STACK_UNWIND_STRICT(create,
			    frame,
			    op_ret,
			    op_errno,
			    local_fd,
			    local_inode,
			    &local->buf,
			    &local->prebuf,
			    &local->postbuf,
			    local_xdata);
	fd_unref(local_fd);
	inode_unref(local_inode);
	if (local_xdata)
		dict_unref(local_xdata);
	return 0;
}

static int crypt_create_tail(call_frame_t *frame,
			     void *cookie,
			     xlator_t *this,
			     int32_t op_ret,
			     int32_t op_errno,
			     dict_t *xdata)
{
	struct gf_flock  lock  = {0, };
	crypt_local_t *local = frame->local;
	fd_t *local_fd = local->fd;
	dict_t *local_xdata = local->xdata;
	inode_t *local_inode = local->inode;

	dict_unref(local->xattr);

	if (op_ret < 0)
		goto error;

        lock.l_type   = F_UNLCK;
        lock.l_whence = SEEK_SET;
        lock.l_start  = 0;
        lock.l_len    = 0;
        lock.l_pid    = 0;

	STACK_WIND(frame,
		   crypt_create_done,
		   FIRST_CHILD(this),
		   FIRST_CHILD(this)->fops->finodelk,
		   this->name,
		   local->fd,
		   F_SETLKW,
		   &lock,
		   NULL);
	return 0;
 error:
	free_inode_info(local->info);
	free_format(local);

	STACK_UNWIND_STRICT(create,
			    frame,
			    op_ret,
			    op_errno,
			    local_fd,
			    local_inode,
			    &local->buf,
			    &local->prebuf,
			    &local->postbuf,
			    local_xdata);

	fd_unref(local_fd);
	inode_unref(local_inode);
	if (local_xdata)
		dict_unref(local_xdata);
	return 0;
}

static int32_t crypt_create_finodelk_cbk(call_frame_t *frame,
					 void *cookie,
					 xlator_t *this,
					 int32_t op_ret,
					 int32_t op_errno,
					 dict_t *xdata)
{
	crypt_local_t *local = frame->local;
	struct crypt_inode_info *info = local->info;

	if (op_ret < 0)
		goto error;

	STACK_WIND(frame,
		   crypt_create_tail,
		   FIRST_CHILD(this),
		   FIRST_CHILD(this)->fops->fsetxattr,
		   local->fd,
		   local->xattr, /* CRYPTO_FORMAT_PREFIX */
		   0,
		   NULL);
	return 0;
 error: 
	free_inode_info(info);
	free_format(local);
	fd_unref(local->fd);
	dict_unref(local->xattr);
	if (local->xdata)
		dict_unref(local->xdata);

	STACK_UNWIND_STRICT(create,
			    frame,
			    op_ret,
			    op_errno,
			    NULL,
			    NULL,
			    NULL,
			    NULL,
			    NULL,
			    NULL);
	return 0;
}

/*
 * Create and store crypt-specific format on disk;
 * Populate cache with private inode info
 */
static int32_t crypt_create_cbk(call_frame_t *frame,
				void *cookie,
				xlator_t *this,
				int32_t op_ret,
				int32_t op_errno,
				fd_t *fd,
				inode_t *inode,
				struct iatt *buf,
				struct iatt *preparent,
				struct iatt *postparent,
				dict_t *xdata)
{
	struct gf_flock lock = {0, };
	crypt_local_t *local = frame->local;
	struct crypt_inode_info *info = local->info;

	if (op_ret < 0)
		goto error;
	if (xdata)
		local->xdata = dict_ref(xdata);	
	local->inode = inode_ref(inode);
	local->buf = *buf;
	local->prebuf = *preparent;
	local->postbuf = *postparent;

        lock.l_len    = 0;
        lock.l_start  = 0;
        lock.l_type   = F_WRLCK;
        lock.l_whence = SEEK_SET;

        STACK_WIND(frame,
		   crypt_create_finodelk_cbk,
		   FIRST_CHILD(this),
		   FIRST_CHILD(this)->fops->finodelk,
		   this->name,
		   local->fd,
		   F_SETLKW,
		   &lock,
		   NULL);	
	return 0;
 error:
	free_inode_info(info);
	free_format(local);
	fd_unref(local->fd);
	dict_unref(local->xattr);

	STACK_UNWIND_STRICT(create,
			    frame,
			    op_ret,
			    op_errno,
			    NULL, NULL, NULL,
			    NULL, NULL, NULL);
	return 0;
}

static int32_t crypt_create(call_frame_t *frame,
			    xlator_t *this,
			    loc_t *loc,
			    int32_t flags,
			    mode_t mode,
			    mode_t umask,
			    fd_t *fd,
			    dict_t *xdata)
{
	int ret;
	data_t *data;
	crypt_local_t *local;
	crypt_private_t *priv;
	struct master_cipher_info *master;
	struct crypt_inode_info *info;

	priv = this->private;
	master = get_master_cinfo(priv);

	if (master_alg_atomic(master)) {
		/*
		 * We can't open O_WRONLY, because we
		 * need to do read-modify-write.
		 */
		if ((flags & O_ACCMODE) == O_WRONLY)
			flags = (flags & ~O_ACCMODE) | O_RDWR;
		/*
		 * Make sure that out translated offsets
		 * and counts won't be ignored
		 */
		flags &= ~O_APPEND;
	}
	local = crypt_alloc_local(frame, this, GF_FOP_CREATE);
	if (!local) {
		ret = ENOMEM;
		goto error;
	}
	data = dict_get(xdata, "gfid-req");
	if (!data) {
		ret = EINVAL;
		gf_log("crypt", GF_LOG_WARNING, "gfid not found");
		goto error;
	}
	if (data->len != sizeof(uuid_t)) {
		ret = EINVAL;
		gf_log("crypt", GF_LOG_WARNING,
		       "bad gfid size (%d), should be %d",
		       (int)data->len, (int)sizeof(uuid_t));
		goto error;
	}
	info = alloc_inode_info(local, loc);
	if (!info){
		ret = ENOMEM;
		goto error;
	}
	/*
	 * NOTE:
	 * format has to be created BEFORE
	 * proceeding to the untrusted server
	 */
	ret = alloc_format_create(local);
	if (ret) {
		free_inode_info(info);
		goto error;
	}
	init_inode_info_create(info, master, data);

	ret = create_format(local->format,
			    loc,
			    info,
			    master);
	if (ret) {
		free_inode_info(info);
		goto error;		
	}
	local->xattr = dict_new();
	if (!local->xattr) {
		free_inode_info(info);
		free_format(local);	
		goto error;
	}
	ret = dict_set_static_bin(local->xattr,
				  CRYPTO_FORMAT_PREFIX,
				  local->format,
				  new_format_size());
	if (ret) {
		dict_unref(local->xattr);
		free_inode_info(info);
		free_format(local);
		goto error;
	}
	ret = dict_set(local->xattr, FSIZE_XATTR_PREFIX, data_from_uint64(0));
	if (ret) {
		dict_unref(local->xattr);
		free_inode_info(info);
		free_format(local);
		goto error;
	}
	local->fd = fd_ref(fd);

	STACK_WIND(frame,
		   crypt_create_cbk,
		   FIRST_CHILD(this),
		   FIRST_CHILD(this)->fops->create,
		   loc,
		   flags,
		   mode,
		   umask,
		   fd,
		   xdata);
	return 0;
 error:
	gf_log("crypt", GF_LOG_WARNING, "can not create file");
	STACK_UNWIND_STRICT(create,
			    frame,
			    -1,
			    ret,
			    NULL, NULL, NULL,
			    NULL, NULL, NULL);
	return 0;
}

/*
 * FIXME: this should depends on the version of format string
 */
static int32_t filter_crypt_xattr(dict_t *dict,
				  char *key, data_t *value, void *data)
{
        dict_del(dict, key);
        return 0;
}

static int32_t crypt_fsetxattr(call_frame_t *frame,
			       xlator_t *this,
			       fd_t *fd,
			       dict_t *dict,
			       int32_t flags, dict_t *xdata)
{
	dict_foreach_fnmatch(dict, "trusted.glusterfs.crypt*",
			     filter_crypt_xattr, NULL);
	STACK_WIND(frame,
		   default_fsetxattr_cbk,
		   FIRST_CHILD(this),
		   FIRST_CHILD(this)->fops->fsetxattr,
		   fd,
		   dict,
		   flags,
		   xdata);
	return 0;
}

/*
 * TBD: verify file metadata before wind
 */
static int32_t crypt_setxattr(call_frame_t *frame,
			      xlator_t *this,
			      loc_t *loc,
			      dict_t *dict,
			      int32_t flags, dict_t *xdata)
{
	dict_foreach_fnmatch(dict, "trusted.glusterfs.crypt*",
			     filter_crypt_xattr, NULL);
	STACK_WIND(frame,
		   default_setxattr_cbk,
		   FIRST_CHILD(this),
		   FIRST_CHILD(this)->fops->setxattr,
		   loc,
		   dict,
		   flags,
		   xdata);
	return 0;
}

/*
 * called as flush_cbk()
 */
static int32_t linkop_end(call_frame_t *frame,
			  void *cookie,
			  xlator_t *this,
			  int32_t op_ret,
			  int32_t op_errno,
			  dict_t *xdata)
{
	crypt_local_t *local = frame->local;
	linkop_unwind_handler_t unwind_fn;
	unwind_fn = linkop_unwind_dispatch(local->fop);

	local->op_ret = op_ret;
	local->op_errno = op_errno;

	if (op_ret < 0 &&
	    op_errno == ENOENT &&
	    local->loc->inode->ia_type == IA_IFLNK) {
		local->op_ret = 0;
		local->op_errno = 0;
	}
	unwind_fn(frame);
	return 0;
}

/*
 * unpin inode on the server
 */
static int32_t link_flush(call_frame_t *frame,
			  void *cookie,
			  xlator_t *this,
			  int32_t op_ret,
			  int32_t op_errno,
			  inode_t *inode,
			  struct iatt *buf,
			  struct iatt *preparent,
			  struct iatt *postparent, dict_t *xdata)
{
	crypt_local_t *local = frame->local;

	if (op_ret < 0)
		goto error;
	if (local->xdata) {
		dict_unref(local->xdata);
		local->xdata = NULL;
	}
	if (xdata)
		local->xdata = dict_ref(xdata);
	local->inode = inode_ref(inode);
	local->buf = *buf;
	local->prebuf = *preparent;
	local->postbuf = *postparent;

	STACK_WIND(frame,
		   linkop_end,
		   FIRST_CHILD(this),
		   FIRST_CHILD(this)->fops->flush,
		   local->fd,
		   NULL);
	return 0;
 error:
	local->op_ret = -1;
	local->op_errno = op_errno;
	link_unwind(frame);
	return 0;
}

void link_unwind(call_frame_t *frame)
{
	crypt_local_t *local = frame->local;
	dict_t *xdata;
	dict_t *xattr;
	inode_t *inode;

	if (!local) {
		STACK_UNWIND_STRICT(link,
				    frame,
				    -1,
				    ENOMEM,
				    NULL,
				    NULL,
				    NULL,
				    NULL,
				    NULL);
		return;
	}
	xdata = local->xdata;
	xattr = local->xattr;
	inode = local->inode;

	if (local->loc){
		loc_wipe(local->loc);
		GF_FREE(local->loc);
	}
	if (local->newloc) {
		loc_wipe(local->newloc);
		GF_FREE(local->newloc);
	}
	if (local->fd)
		fd_unref(local->fd);
	if (local->format)
		GF_FREE(local->format);

	STACK_UNWIND_STRICT(link,
			    frame,
			    local->op_ret,
			    local->op_errno,
			    inode,
			    &local->buf,
			    &local->prebuf,
			    &local->postbuf,
			    xdata);
	if (xdata)
		dict_unref(xdata);
	if (xattr)
		dict_unref(xattr);
	if (inode)
		inode_unref(inode);
}

void link_wind(call_frame_t *frame, xlator_t *this)
{
	crypt_local_t *local = frame->local;

	STACK_WIND(frame,
		   link_flush,
		   FIRST_CHILD(this),
		   FIRST_CHILD(this)->fops->link,
		   local->loc,
		   local->newloc,
		   local->xdata);
}

/*
 * unlink()
 */
static int32_t unlink_flush(call_frame_t *frame,
			    void *cookie,
			    xlator_t *this,
			    int32_t op_ret,
			    int32_t op_errno,
			    struct iatt *preparent,
			    struct iatt *postparent, dict_t *xdata)
{
	crypt_local_t *local = frame->local;

	if (op_ret < 0)
		goto error;
	local->prebuf = *preparent;
	local->postbuf = *postparent;
	if (local->xdata) {
		dict_unref(local->xdata);
		local->xdata = NULL;
	}
	if (xdata)
		local->xdata = dict_ref(xdata);

	STACK_WIND(frame,
		   linkop_end,
		   FIRST_CHILD(this),
		   FIRST_CHILD(this)->fops->flush,
		   local->fd,
		   NULL);
	return 0;
 error:
	local->op_ret = -1;
	local->op_errno = op_errno;
	unlink_unwind(frame);
	return 0;
}

void unlink_unwind(call_frame_t *frame)
{
	crypt_local_t *local = frame->local;
	dict_t *xdata;
	dict_t *xattr;

	if (!local) {
		STACK_UNWIND_STRICT(unlink,
				    frame,
				    -1,
				    ENOMEM,
				    NULL,
				    NULL,
				    NULL);
		return;
	}
	xdata = local->xdata;
	xattr = local->xattr;
	if (local->loc){
		loc_wipe(local->loc);
		GF_FREE(local->loc);
	}
	if (local->fd)
		fd_unref(local->fd);
	if (local->format)
		GF_FREE(local->format);

	STACK_UNWIND_STRICT(unlink,
			    frame,
			    local->op_ret,
			    local->op_errno,
			    &local->prebuf,
			    &local->postbuf,
			    xdata);
	if (xdata)
		dict_unref(xdata);
	if (xattr)
		dict_unref(xattr);
}

void unlink_wind(call_frame_t *frame, xlator_t *this)
{
	crypt_local_t *local = frame->local;

	STACK_WIND(frame,
		   unlink_flush,
		   FIRST_CHILD(this),
		   FIRST_CHILD(this)->fops->unlink,
		   local->loc,
		   local->flags,
		   local->xdata);
}

void rename_unwind(call_frame_t *frame)
{
	crypt_local_t *local = frame->local;
	dict_t *xdata;
	dict_t *xattr;
	struct iatt *prenewparent;
	struct iatt *postnewparent;

	if (!local) {
		STACK_UNWIND_STRICT(rename,
				    frame,
				    -1,
				    ENOMEM,
				    NULL,
				    NULL,
				    NULL,
				    NULL,
				    NULL,
				    NULL);	
		return;
	}
	xdata = local->xdata;
	xattr = local->xattr;
	prenewparent = local->prenewparent;
	postnewparent = local->postnewparent;	

	if (local->loc){
		loc_wipe(local->loc);
		GF_FREE(local->loc);
	}
	if (local->newloc){
		loc_wipe(local->newloc);
		GF_FREE(local->newloc);
	}
	if (local->fd)
		fd_unref(local->fd);
	if (local->format)
		GF_FREE(local->format);

	STACK_UNWIND_STRICT(rename,
			    frame,
			    local->op_ret,
			    local->op_errno,
			    &local->buf,
			    &local->prebuf,
			    &local->postbuf,
			    prenewparent,
			    postnewparent,
			    xdata);
	if (xdata)
		dict_unref(xdata);
	if (xattr)
		dict_unref(xattr);
	if (prenewparent)
		GF_FREE(prenewparent);
	if (postnewparent)
		GF_FREE(postnewparent);
}

/*
 * called as flush_cbk()
 */
static int32_t rename_end(call_frame_t *frame,
			  void *cookie,
			  xlator_t *this,
			  int32_t op_ret,
			  int32_t op_errno,
			  dict_t *xdata)
{
	crypt_local_t *local = frame->local;

	local->op_ret = op_ret;
	local->op_errno = op_errno;

	rename_unwind(frame);
	return 0;
}

static int32_t rename_flush(call_frame_t *frame,
			    void *cookie,
			    xlator_t *this,
			    int32_t op_ret,
			    int32_t op_errno,
			    struct iatt *buf,
			    struct iatt *preoldparent,
			    struct iatt *postoldparent,
			    struct iatt *prenewparent,
			    struct iatt *postnewparent,
			    dict_t *xdata)
{
	crypt_local_t *local = frame->local;

	if (op_ret < 0)
		goto error;
	dict_unref(local->xdata);
	local->xdata = NULL;
	if (xdata)
		local->xdata = dict_ref(xdata);

	local->buf = *buf;
	local->prebuf = *preoldparent;
	local->postbuf = *postoldparent;
	if (prenewparent) {
		local->prenewparent = GF_CALLOC(1, sizeof(*prenewparent),
						gf_crypt_mt_iatt);
		if (!local->prenewparent) {
			op_errno = ENOMEM;
			goto error;
		}
		*local->prenewparent = *prenewparent;
	}
	if (postnewparent) {
		local->postnewparent = GF_CALLOC(1, sizeof(*postnewparent),
						 gf_crypt_mt_iatt);
		if (!local->postnewparent) {
			op_errno = ENOMEM;
			goto error;
		}
		*local->postnewparent = *postnewparent;
	}
	STACK_WIND(frame,
		   rename_end,
		   FIRST_CHILD(this),
		   FIRST_CHILD(this)->fops->flush,
		   local->fd,
		   NULL);
	return 0;
 error:
	local->op_ret = -1;
	local->op_errno = op_errno;
	rename_unwind(frame);
	return 0;
}

void rename_wind(call_frame_t *frame, xlator_t *this)
{
	crypt_local_t *local = frame->local;

	STACK_WIND(frame,
		   rename_flush,
		   FIRST_CHILD(this),
		   FIRST_CHILD(this)->fops->rename,
		   local->loc,
		   local->newloc,
		   local->xdata);
}

static int32_t __do_linkop(call_frame_t *frame,
			   void *cookie,
			   xlator_t *this,
			   int32_t op_ret,
			   int32_t op_errno, dict_t *xdata)
{
	crypt_local_t *local = frame->local;
	linkop_wind_handler_t wind_fn;
	linkop_unwind_handler_t unwind_fn;

	wind_fn = linkop_wind_dispatch(local->fop);
	unwind_fn = linkop_unwind_dispatch(local->fop);

	local->op_ret = op_ret;
	local->op_errno = op_errno;

	if (op_ret >= 0)
		wind_fn(frame, this);
	else {
		gf_log(this->name, GF_LOG_WARNING, "mtd unlock failed (%d)",
		       op_errno);
		unwind_fn(frame);
	}
	return 0;
}

static int32_t do_linkop(call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno,
			 dict_t *xdata)
{
	struct gf_flock  lock  = {0, };
	crypt_local_t *local = frame->local;
	linkop_unwind_handler_t unwind_fn;

	unwind_fn = linkop_unwind_dispatch(local->fop);
	local->op_ret = op_ret;
	local->op_errno = op_errno;

	if(op_ret < 0)
		goto error;

	lock.l_type   = F_UNLCK;
        lock.l_whence = SEEK_SET;
        lock.l_start  = 0;
        lock.l_len    = 0;
        lock.l_pid    = 0;

	STACK_WIND(frame,
		   __do_linkop,
		   FIRST_CHILD(this),
		   FIRST_CHILD(this)->fops->finodelk,
		   this->name,
		   local->fd,
		   F_SETLKW,
		   &lock,
		   NULL);
	return 0;
 error:		
	unwind_fn(frame);
	return 0;
}

/*
 * Update the metadata string (against the new pathname);
 * submit the result
 */
static int32_t linkop_begin(call_frame_t *frame,
			    void *cookie,
			    xlator_t *this,
			    int32_t op_ret,
			    int32_t op_errno,
			    fd_t *fd,
			    dict_t *xdata)
{
	gf_boolean_t upload_info;
	crypt_local_t *local = frame->local;
	crypt_private_t *priv = this->private;
	struct crypt_inode_info *info;
	data_t *old_mtd;
	uint32_t new_mtd_size;
	uint64_t value = 0;
	void (*unwind_fn)(call_frame_t *frame);
	void (*wind_fn)(call_frame_t *frame, xlator_t *this);
	mtd_op_t mop;

	wind_fn = linkop_wind_dispatch(local->fop);
	unwind_fn = linkop_unwind_dispatch(local->fop);
	mop = linkop_mtdop_dispatch(local->fop);

	if (local->fd->inode->ia_type == IA_IFLNK)
		goto wind;
	if (op_ret < 0)
		/*
		 * verification failed
		 */
		goto error;

	old_mtd = dict_get(xdata, CRYPTO_FORMAT_PREFIX);
	if (!old_mtd) {
		op_errno = EIO;
		gf_log (this->name, GF_LOG_DEBUG,
			"Metadata string wasn't found");
		goto error;
	}
	new_mtd_size = format_size(mop, old_mtd->len);
	op_errno = alloc_format(local, new_mtd_size);
	if (op_errno)
		goto error;
	/*
	 * check for cached info 
	 */
	op_ret = inode_ctx_get(fd->inode, this, &value);
	if (op_ret != -1) {
		info = (struct crypt_inode_info *)(long)value;
		if (info == NULL) {
			gf_log (this->name, GF_LOG_WARNING,
				"Inode info was not found");
			op_errno = EINVAL;
			goto error;
		}
		/*
		 * info was found in the cache
		 */
		local->info = info;
		upload_info = _gf_false;
	}
	else {
		/*
		 * info wasn't found in the cache;
		 */
		info = alloc_inode_info(local, local->loc);
		if (!info)
			goto error;
		init_inode_info_head(info, fd);
		local->info = info;
		upload_info = _gf_true;
	}
	op_errno = open_format((unsigned char *)old_mtd->data,
			       old_mtd->len,
			       local->loc,
			       info,
			       get_master_cinfo(priv),
			       local,
			       upload_info);
	if (op_errno)
		goto error;
	if (upload_info == _gf_true) {
		op_errno = init_inode_info_tail(info,
						get_master_cinfo(priv));
		if (op_errno)
			goto error;
		op_errno = inode_ctx_put(fd->inode, this,
					 (uint64_t)(long)(info));
		if (op_errno == -1) {
			op_errno = EIO;
			goto error;
		}
	}
	/*
	 * update the format string (append/update/cup a MAC)
	 */
	op_errno = update_format(local->format,
				 (unsigned char *)old_mtd->data,
				 old_mtd->len,
				 local->mac_idx,
				 mop,
				 local->newloc,
				 info,
				 get_master_cinfo(priv),
				 local);
	if (op_errno)
		goto error;
	/*
	 * store the new format string on the server
	 */
	if (new_mtd_size) {
		op_errno = dict_set_static_bin(local->xattr,
					       CRYPTO_FORMAT_PREFIX,
					       local->format,
					       new_mtd_size);
		if (op_errno)
			goto error;
	}
	STACK_WIND(frame,
		   do_linkop,
		   FIRST_CHILD(this),
		   FIRST_CHILD(this)->fops->setxattr,
		   local->loc,
		   local->xattr,
		   0,
		   NULL);
	return 0;
 wind:
	wind_fn(frame, this);
	return 0;
 error:
	local->op_ret = -1;
	local->op_errno = op_errno;
	unwind_fn(frame);
	return 0;
}

static int32_t linkop_grab_local(call_frame_t *frame,
				 xlator_t *this,
				 loc_t *oldloc,
				 loc_t *newloc,
				 int flags, dict_t *xdata,
				 glusterfs_fop_t op)
{
	int32_t ret = ENOMEM;
	fd_t *fd;
	crypt_local_t *local;

	local = crypt_alloc_local(frame, this, op);
	if (!local)
		goto error;
	if (xdata)
		local->xdata = dict_ref(xdata);

	fd = fd_create(oldloc->inode, frame->root->pid);
	if (!fd) {
		gf_log(this->name, GF_LOG_ERROR, "Can not create fd");
		goto error;
	}
	local->fd = fd;
	local->flags = flags;
	local->loc = GF_CALLOC(1, sizeof(*oldloc), gf_crypt_mt_loc);
	if (!local->loc)
		goto error;
	memset(local->loc, 0, sizeof(*local->loc));
	ret = loc_copy(local->loc, oldloc);
	if (ret) {
		GF_FREE(local->loc);
		local->loc = NULL;
		goto error;
	}
	if (newloc) {
		local->newloc = GF_CALLOC(1, sizeof(*newloc), gf_crypt_mt_loc);
		if (!local->newloc) {
			loc_wipe(local->loc);
			GF_FREE(local->loc);
			goto error;
		}
		memset(local->newloc, 0, sizeof(*local->newloc));
		ret = loc_copy(local->newloc, newloc);
		if (ret) {
			loc_wipe(local->loc);
			GF_FREE(local->loc);
			GF_FREE(local->newloc);
			goto error;
		}
	}
	local->xattr = dict_new();
	if (!local->xattr) {
		gf_log(this->name, GF_LOG_ERROR, "Can not create dict");
		ret = ENOMEM;
		goto error;
	}
	return 0;

error:
        if (local) {
                if (local->xdata)
                        dict_unref(local->xdata);
                if (local->fd)
                        fd_unref(local->fd);
                local->fd = 0;
                local->loc = NULL;
                local->newloc = NULL;
                local->op_ret = -1;
                local->op_errno = ret;
        }

        return ret;
}

/*
 * read and verify locked metadata against the old pathname (via open);
 * update the metadata string in accordance with the new pathname;
 * submit modified metadata;
 * wind;
 */
static int32_t linkop(call_frame_t *frame,
		      xlator_t *this,
		      loc_t *oldloc,
		      loc_t *newloc,
		      int flags,
		      dict_t *xdata,
		      glusterfs_fop_t op)
{
	int32_t ret;
	dict_t *dict;
	crypt_local_t *local;
	void (*unwind_fn)(call_frame_t *frame);

	unwind_fn = linkop_unwind_dispatch(op);

	ret = linkop_grab_local(frame, this, oldloc, newloc, flags, xdata, op);
	local = frame->local;
	if (ret)
		goto error;
	dict = dict_new();
	if (!dict) {
		gf_log(this->name, GF_LOG_ERROR, "Can not create dict");
		ret = ENOMEM;
		goto error;
	}
	/*
	 * Set a message to crypt_open() that we need
	 * locked metadata string.
	 * All link operations (link, unlink, rename)
	 * need write lock
	 */
	msgflags_set_mtd_wlock(&local->msgflags);
	ret = dict_set_static_bin(dict,
				  MSGFLAGS_PREFIX,
				  &local->msgflags,
				  sizeof(local->msgflags));
	if (ret) {
		gf_log(this->name, GF_LOG_ERROR, "Can not set dict");
		dict_unref(dict);
		goto error;
	}
	/*
	 * verify metadata against the old pathname
	 * and retrieve locked metadata string
	 */
	STACK_WIND(frame,
		   linkop_begin,
		   this,
		   this->fops->open, /* crypt_open() */
		   oldloc,
		   O_RDWR,
		   local->fd,
		   dict);
	dict_unref(dict);
	return 0;
 error:
	local->op_ret = -1;
	local->op_errno = ret;
	unwind_fn(frame);
	return 0;
}

static int32_t crypt_link(call_frame_t *frame, xlator_t *this,
			  loc_t *oldloc, loc_t *newloc, dict_t *xdata)
{
	return linkop(frame, this, oldloc, newloc, 0, xdata, GF_FOP_LINK);
}

static int32_t crypt_unlink(call_frame_t *frame, xlator_t *this,
			    loc_t *loc, int flags, dict_t *xdata)
{
	return linkop(frame, this, loc, NULL, flags, xdata, GF_FOP_UNLINK);
}

static int32_t crypt_rename(call_frame_t *frame, xlator_t *this,
			    loc_t *oldloc, loc_t *newloc, dict_t *xdata)
{
	return linkop(frame, this, oldloc, newloc, 0, xdata, GF_FOP_RENAME);
}

static void put_one_call_open(call_frame_t *frame)
{
	crypt_local_t *local = frame->local;
	if (put_one_call(local)) {
		fd_t *fd = local->fd;
		loc_t *loc = local->loc;
		dict_t *xdata = local->xdata;

		STACK_UNWIND_STRICT(open,
				    frame,
				    local->op_ret,
				    local->op_errno,
				    fd,
				    xdata);
		fd_unref(fd);
		if (xdata)
			dict_unref(xdata);
		loc_wipe(loc);
		GF_FREE(loc);
	}
}

static int32_t __crypt_readv_done(call_frame_t *frame,
				  void *cookie,
				  xlator_t *this,
				  int32_t op_ret,
				  int32_t op_errno, dict_t *xdata)
{
	crypt_local_t *local = frame->local;
	fd_t *local_fd = local->fd;
	dict_t *local_xdata = local->xdata;
	/* read deals with data configs only */
	struct iovec *avec = local->data_conf.avec;
	char **pool = local->data_conf.pool;
	int blocks_in_pool = local->data_conf.blocks_in_pool;
	struct iobref *iobref = local->iobref;
	struct iobref *iobref_data = local->iobref_data;

	if (op_ret < 0) {
		gf_log(this->name, GF_LOG_WARNING,
		       "readv unlock failed (%d)", op_errno);
		if (local->op_ret >= 0) {
			local->op_ret = op_ret;
			local->op_errno = op_errno;
		}
	}
	dump_plain_text(local, avec);

	gf_log("crypt", GF_LOG_DEBUG,
	       "readv: ret_to_user: %d, iovec len: %d, ia_size: %llu",
	       (int)(local->rw_count > 0 ? local->rw_count : local->op_ret),
	       (int)(local->rw_count > 0 ? iovec_get_size(avec, local->data_conf.acount) : 0),
	       (unsigned long long)local->buf.ia_size);

	STACK_UNWIND_STRICT(readv,
			    frame,
			    local->rw_count > 0 ? local->rw_count : local->op_ret,
			    local->op_errno,
			    avec,
			    avec ? local->data_conf.acount : 0,
			    &local->buf,
			    local->iobref,
			    local_xdata);

	free_avec(avec, pool, blocks_in_pool);
	fd_unref(local_fd);
	if (local_xdata)
		dict_unref(local_xdata);
	if (iobref)
		iobref_unref(iobref);
	if (iobref_data)
		iobref_unref(iobref_data);
	return 0;
}

static void crypt_readv_done(call_frame_t *frame, xlator_t *this)
{
	if (parent_is_crypt_xlator(frame, this))
		/*
		 *  don't unlock (it will be done by the parent)
		 */
		__crypt_readv_done(frame, NULL, this, 0, 0, NULL);
	else {
		crypt_local_t *local = frame->local;
		struct gf_flock  lock  = {0, };

		lock.l_type   = F_UNLCK;
		lock.l_whence = SEEK_SET;
		lock.l_start  = 0;
		lock.l_len    = 0;
		lock.l_pid    = 0;

		STACK_WIND(frame,
			   __crypt_readv_done,
			   FIRST_CHILD(this),
			   FIRST_CHILD(this)->fops->finodelk,
			   this->name,
			   local->fd,
			   F_SETLKW,
			   &lock,
			   NULL);
	}
}

static void put_one_call_readv(call_frame_t *frame, xlator_t *this)
{
	crypt_local_t *local = frame->local;
	if (put_one_call(local))
		crypt_readv_done(frame, this);
}

static int32_t __crypt_writev_done(call_frame_t *frame,
				   void *cookie,
				   xlator_t *this,
				   int32_t op_ret,
				   int32_t op_errno, dict_t *xdata)
{
	crypt_local_t *local = frame->local;
	fd_t *local_fd = local->fd;
	dict_t *local_xdata = local->xdata;
	int32_t ret_to_user;

	if (local->xattr)
		dict_unref(local->xattr);
	/*
	 * Calculate amout of butes to be returned
	 * to user. We need to subtract paddings that
	 * have been written as a part of atom.
	 */
	/*
	 * subtract head padding
	 */
	if (local->rw_count == 0)
		/*
		 * Nothing has been written, it must be an error
		 */
		ret_to_user = local->op_ret;
	else if (local->rw_count <= local->data_conf.off_in_head) {
		gf_log("crypt", GF_LOG_WARNING, "Incomplete write");
		ret_to_user = 0;
	}
	else
		ret_to_user = local->rw_count -
			local->data_conf.off_in_head;
	/*
	 * subtract tail padding
	 */
	if (ret_to_user > local->data_conf.orig_size)
		ret_to_user = local->data_conf.orig_size;

	if (local->iobref)
		iobref_unref(local->iobref);
	if (local->iobref_data)
		iobref_unref(local->iobref_data);
	free_avec_data(local);
	free_avec_hole(local);

	gf_log("crypt", GF_LOG_DEBUG,
	       "writev: ret_to_user: %d", ret_to_user);

	STACK_UNWIND_STRICT(writev,
			    frame,
			    ret_to_user,
			    local->op_errno,
			    &local->prebuf,
			    &local->postbuf,
			    local_xdata);
	fd_unref(local_fd);
	if (local_xdata)
		dict_unref(local_xdata);
	return 0;
}

static int32_t crypt_writev_done(call_frame_t *frame,
				 void *cookie,
				 xlator_t *this,
				 int32_t op_ret,
				 int32_t op_errno,
				 dict_t *xdata)
{
	crypt_local_t *local = frame->local;

	if (op_ret < 0)
		gf_log("crypt", GF_LOG_WARNING, "can not update file size");

	if (parent_is_crypt_xlator(frame, this))
		/*
		 * don't unlock (it will be done by the parent)
		 */
		__crypt_writev_done(frame, NULL, this, 0, 0, NULL);
	else {
		struct gf_flock  lock  = {0, };

		lock.l_type   = F_UNLCK;
		lock.l_whence = SEEK_SET;
		lock.l_start  = 0;
		lock.l_len    = 0;
		lock.l_pid    = 0;

		STACK_WIND(frame,
			   __crypt_writev_done,
			   FIRST_CHILD(this),
			   FIRST_CHILD(this)->fops->finodelk,
			   this->name,
			   local->fd,
			   F_SETLKW,
			   &lock,
			   NULL);
	}
	return 0;
}

static void put_one_call_writev(call_frame_t *frame, xlator_t *this)
{
	crypt_local_t *local = frame->local;
	if (put_one_call(local)) {
		if (local->update_disk_file_size) {
			int32_t ret;
			/*
			 * update file size, unlock the file and unwind
			 */
			ret = dict_set(local->xattr,
				       FSIZE_XATTR_PREFIX,
				       data_from_uint64(local->cur_file_size));
			if (ret) {
				gf_log("crypt", GF_LOG_WARNING,
				       "can not set key to update file size");
				crypt_writev_done(frame, NULL,
						  this, 0, 0, NULL);
				return;
			}
			gf_log("crypt", GF_LOG_DEBUG,
			       "Updating disk file size to %llu",
			       (unsigned long long)local->cur_file_size);
			STACK_WIND(frame,
				   crypt_writev_done,
				   FIRST_CHILD(this),
				   FIRST_CHILD(this)->fops->fsetxattr,
				   local->fd,
				   local->xattr, /* CRYPTO_FORMAT_PREFIX */
				   0,
				   NULL);
		}
		else
			crypt_writev_done(frame, NULL, this, 0, 0, NULL);
	}
}

static int32_t __crypt_ftruncate_done(call_frame_t *frame,
				      void *cookie,
				      xlator_t *this,
				      int32_t op_ret,
				      int32_t op_errno, dict_t *xdata)
{
	crypt_local_t *local = frame->local;
	fd_t *local_fd = local->fd;
	dict_t *local_xdata = local->xdata;
	char *iobase = local->vec.iov_base;

	if (op_ret < 0) {
		gf_log(this->name, GF_LOG_WARNING,
		       "ftruncate unlock failed (%d)", op_errno);
		if (local->op_ret >= 0) {
			local->op_ret = op_ret;
			local->op_errno = op_errno;
		}
	}
	if (local->iobref_data)
		iobref_unref(local->iobref_data);
	free_avec_data(local);
	free_avec_hole(local);

	gf_log("crypt", GF_LOG_DEBUG,
	       "ftruncate, return to user: presize=%llu, postsize=%llu",
	       (unsigned long long)local->prebuf.ia_size,
	       (unsigned long long)local->postbuf.ia_size);

	STACK_UNWIND_STRICT(ftruncate,
			    frame,
			    local->op_ret < 0 ? -1 : 0,
			    local->op_errno,
			    &local->prebuf,
			    &local->postbuf,
			    local_xdata);
	fd_unref(local_fd);
	if (local_xdata)
		dict_unref(local_xdata);
	if (iobase)
		GF_FREE(iobase);
	return 0;
}

static int32_t crypt_ftruncate_done(call_frame_t *frame,
				    void *cookie,
				    xlator_t *this,
				    int32_t op_ret,
				    int32_t op_errno,
				    dict_t *xdata)
{
	crypt_local_t *local = frame->local;
	struct gf_flock  lock  = {0, };

	dict_unref(local->xattr);
	if (op_ret < 0)
		gf_log("crypt", GF_LOG_WARNING, "can not update file size");

	lock.l_type   = F_UNLCK;
	lock.l_whence = SEEK_SET;
	lock.l_start  = 0;
	lock.l_len    = 0;
	lock.l_pid    = 0;

	STACK_WIND(frame,
		   __crypt_ftruncate_done,
		   FIRST_CHILD(this),
		   FIRST_CHILD(this)->fops->finodelk,
		   this->name,
		   local->fd,
		   F_SETLKW,
		   &lock,
		   NULL);
	return 0;
}

static void put_one_call_ftruncate(call_frame_t *frame, xlator_t *this)
{
	crypt_local_t *local = frame->local;
	if (put_one_call(local)) {
		if (local->update_disk_file_size) {
			int32_t ret;
			/*
			 * update file size, unlock the file and unwind
			 */
			ret = dict_set(local->xattr,
				       FSIZE_XATTR_PREFIX,
				       data_from_uint64(local->cur_file_size));
			if (ret) {
				gf_log("crypt", GF_LOG_WARNING,
				       "can not set key to update file size");
				crypt_ftruncate_done(frame, NULL,
						     this, 0, 0, NULL);
				return;
			}
			gf_log("crypt", GF_LOG_DEBUG,
			       "Updating disk file size to %llu",
			       (unsigned long long)local->cur_file_size);
			STACK_WIND(frame,
				   crypt_ftruncate_done,
				   FIRST_CHILD(this),
				   FIRST_CHILD(this)->fops->fsetxattr,
				   local->fd,
				   local->xattr, /* CRYPTO_FORMAT_PREFIX */
				   0,
				   NULL);
		}
		else
			crypt_ftruncate_done(frame, NULL, this, 0, 0, NULL);
	}
}

/*
 * load regular file size for some FOPs
 */
static int32_t load_file_size(call_frame_t *frame,
			      void *cookie,
			      xlator_t *this,
			      int32_t op_ret,
			      int32_t op_errno,
			      dict_t *dict,
			      dict_t *xdata)
{
	data_t *data;
	crypt_local_t *local = frame->local;

	dict_t *local_xdata = local->xdata;
	inode_t *local_inode = local->inode;

	if (op_ret < 0)
		goto unwind;
	/*
	 * load regular file size
	 */
	data = dict_get(dict, FSIZE_XATTR_PREFIX);
	if (!data) {
		if (local->xdata)
			dict_unref(local->xdata);
		gf_log("crypt", GF_LOG_WARNING, "Regular file size not found");
		op_ret = -1;
		op_errno = EIO;
		goto unwind;
	}
	local->buf.ia_size = data_to_uint64(data);

	gf_log(this->name, GF_LOG_DEBUG,
	       "FOP %d: Translate regular file to %llu",
	       local->fop,
	       (unsigned long long)local->buf.ia_size);
 unwind:	
	if (local->fd)
		fd_unref(local->fd);
	if (local->loc) {
		loc_wipe(local->loc);
		GF_FREE(local->loc);
	}
	switch (local->fop) {
	case GF_FOP_FSTAT:
		STACK_UNWIND_STRICT(fstat,
				    frame,
				    op_ret,
				    op_errno,
				    op_ret >= 0 ? &local->buf : NULL,
				    local->xdata);
		break;
	case GF_FOP_STAT:
		STACK_UNWIND_STRICT(stat,
				    frame,
				    op_ret,
				    op_errno,
				    op_ret >= 0 ? &local->buf : NULL,
				    local->xdata);
		break;
	case GF_FOP_LOOKUP:
		STACK_UNWIND_STRICT(lookup,
				    frame,
				    op_ret,
				    op_errno,
				    op_ret >= 0 ? local->inode : NULL,
				    op_ret >= 0 ? &local->buf : NULL,				    
				    local->xdata,
				    op_ret >= 0 ? &local->postbuf : NULL);
		break;
	case GF_FOP_READ:
		STACK_UNWIND_STRICT(readv,
				    frame,
				    op_ret,
				    op_errno,
				    NULL,
				    0,
				    op_ret >= 0 ? &local->buf : NULL,
				    NULL,
				    NULL);
		break;
	default:
		gf_log(this->name, GF_LOG_WARNING,
		       "Improper file operation %d", local->fop);
	}
	if (local_xdata)
		dict_unref(local_xdata);
	if (local_inode)
		inode_unref(local_inode);
	return 0;
}

static int32_t crypt_stat_common_cbk(call_frame_t *frame,
				     void *cookie,
				     xlator_t *this,
				     int32_t op_ret,
				     int32_t op_errno,
				     struct iatt *buf, dict_t *xdata)
{
	crypt_local_t *local = frame->local;

	if (op_ret < 0)
		goto unwind;
	if (!IA_ISREG(buf->ia_type))
		goto unwind;

	local->buf = *buf;
	if (xdata)
		local->xdata = dict_ref(xdata);

	switch (local->fop) {
	case GF_FOP_FSTAT:
		STACK_WIND(frame,
			   load_file_size,
			   FIRST_CHILD(this),
			   FIRST_CHILD(this)->fops->fgetxattr,
			   local->fd,
			   FSIZE_XATTR_PREFIX,
			   NULL);
		break;
	case GF_FOP_STAT:
		STACK_WIND(frame,
			   load_file_size,
			   FIRST_CHILD(this),
			   FIRST_CHILD(this)->fops->getxattr,
			   local->loc,
			   FSIZE_XATTR_PREFIX,
			   NULL);
		break;
	default:
		gf_log (this->name, GF_LOG_WARNING,
			"Improper file operation %d", local->fop);
	}
	return 0;
 unwind:
	if (local->fd)
		fd_unref(local->fd);
	if (local->loc) {
		loc_wipe(local->loc);
		GF_FREE(local->loc);
	}
	switch (local->fop) {
	case GF_FOP_FSTAT:
		STACK_UNWIND_STRICT(fstat,
				    frame,
				    op_ret,
				    op_errno,
				    op_ret >= 0 ? buf : NULL,
				    op_ret >= 0 ? xdata : NULL);
		break;
	case GF_FOP_STAT:
		STACK_UNWIND_STRICT(stat,
				    frame,
				    op_ret,
				    op_errno,
				    op_ret >= 0 ? buf : NULL,
				    op_ret >= 0 ? xdata : NULL);
		break;
	default:
		gf_log (this->name, GF_LOG_WARNING,
			"Improper file operation %d", local->fop);
	}
	return 0;
}

static int32_t crypt_fstat(call_frame_t *frame,
			   xlator_t *this,
			   fd_t *fd, dict_t *xdata)
{
	crypt_local_t *local;

	local = crypt_alloc_local(frame, this, GF_FOP_FSTAT);
	if (!local)
		goto error;
	local->fd = fd_ref(fd);
	STACK_WIND(frame,
		   crypt_stat_common_cbk,
		   FIRST_CHILD(this),
		   FIRST_CHILD(this)->fops->fstat,
		   fd,
		   xdata);	
	return 0;
 error:
	STACK_UNWIND_STRICT(fstat,
			    frame,
			    -1,
			    ENOMEM,
			    NULL,
			    NULL);
	return 0;
}

static int32_t crypt_stat(call_frame_t *frame,
			  xlator_t *this,
			  loc_t *loc, dict_t *xdata)
{
	int32_t ret;
	crypt_local_t *local;

	local = crypt_alloc_local(frame, this, GF_FOP_STAT);
	if (!local)
		goto error;
	local->loc = GF_CALLOC(1, sizeof(*loc), gf_crypt_mt_loc);
	if (!local->loc)
		goto error;
	memset(local->loc, 0, sizeof(*local->loc));
	ret = loc_copy(local->loc, loc);
	if (ret) {
		GF_FREE(local->loc);
		goto error;
	}
	STACK_WIND(frame,
		   crypt_stat_common_cbk,
		   FIRST_CHILD(this),
		   FIRST_CHILD(this)->fops->stat,
		   loc,
		   xdata);	
	return 0;
 error:
	STACK_UNWIND_STRICT(stat,
			    frame,
			    -1,
			    ENOMEM,
			    NULL,
			    NULL);
	return 0;
}

static int32_t crypt_lookup_cbk(call_frame_t *frame,
				void *cookie,
				xlator_t *this,
				int32_t op_ret,
				int32_t op_errno,
				inode_t *inode,
				struct iatt *buf, dict_t *xdata,
				struct iatt *postparent)
{
	crypt_local_t *local = frame->local;

	if (op_ret < 0)
		goto unwind;
	if (!IA_ISREG(buf->ia_type))
		goto unwind;

	local->inode = inode_ref(inode);
	local->buf = *buf;
	local->postbuf = *postparent;
	if (xdata)
		local->xdata = dict_ref(xdata);
	uuid_copy(local->loc->gfid, buf->ia_gfid);

	STACK_WIND(frame,
		   load_file_size,
		   FIRST_CHILD(this),
		   FIRST_CHILD(this)->fops->getxattr,
		   local->loc,
		   FSIZE_XATTR_PREFIX,
		   NULL);
	return 0;
 unwind:
	loc_wipe(local->loc);
	GF_FREE(local->loc);
	STACK_UNWIND_STRICT(lookup,
			    frame,
			    op_ret,
			    op_errno,
			    inode,
			    buf,
			    xdata,
			    postparent);
	return 0;
}

static int32_t crypt_lookup(call_frame_t *frame,
			    xlator_t *this,
			    loc_t *loc, dict_t *xdata)
{
	int32_t ret;
	crypt_local_t *local;

	local = crypt_alloc_local(frame, this, GF_FOP_LOOKUP);
	if (!local)
		goto error;
	local->loc = GF_CALLOC(1, sizeof(*loc), gf_crypt_mt_loc);
	if (!local->loc)
		goto error;
	memset(local->loc, 0, sizeof(*local->loc));
	ret = loc_copy(local->loc, loc);
	if (ret) {
		GF_FREE(local->loc);
		goto error;
	}
	gf_log(this->name, GF_LOG_DEBUG, "Lookup %s", loc->path);
	STACK_WIND(frame,
		   crypt_lookup_cbk,
		   FIRST_CHILD(this),
		   FIRST_CHILD(this)->fops->lookup,
		   loc,
		   xdata);
	return 0;
 error:
	STACK_UNWIND_STRICT(lookup,
			    frame,
			    -1,
			    ENOMEM,
			    NULL,
			    NULL,
			    NULL,
			    NULL);
	return 0;
}

/*
 * for every regular directory entry find its real file size
 * and update stat's buf properly
 */
static int32_t crypt_readdirp_cbk(call_frame_t *frame,
				  void *cookie,
				  xlator_t *this,
				  int32_t op_ret,
				  int32_t op_errno,
				  gf_dirent_t *entries, dict_t *xdata)
{
	gf_dirent_t *entry = NULL;

	if (op_ret < 0)
		goto unwind;

	list_for_each_entry (entry, (&entries->list), list) {
		data_t *data;

		if (!IA_ISREG(entry->d_stat.ia_type))
			continue;
		data = dict_get(entry->dict, FSIZE_XATTR_PREFIX);
		if (!data){
			gf_log("crypt", GF_LOG_WARNING,
			       "Regular file size of direntry not found");
			op_errno = EIO;
			op_ret = -1;
			break;
		}
		entry->d_stat.ia_size = data_to_uint64(data);
	}
 unwind:
	STACK_UNWIND_STRICT(readdirp, frame, op_ret, op_errno, entries, xdata);
	return 0;
}

/*
 * ->readdirp() fills in-core inodes, so we need to set proper
 * file sizes for all directory entries of the parent @fd.
 * Actual updates take place in ->crypt_readdirp_cbk()
 */
static int32_t crypt_readdirp(call_frame_t *frame, xlator_t *this,
			      fd_t *fd, size_t size, off_t offset,
			      dict_t *xdata)
{
	int32_t ret = ENOMEM;

	if (!xdata) {
		xdata = dict_new();
		if (!xdata)
			goto error;
	}
	else
		dict_ref(xdata);
	/*
	 * make sure that we'll have real file sizes at ->readdirp_cbk()
	 */
	ret = dict_set(xdata, FSIZE_XATTR_PREFIX, data_from_uint64(0));
	if (ret) {
		dict_unref(xdata);
		goto error;
	}
	STACK_WIND(frame,
		   crypt_readdirp_cbk,
		   FIRST_CHILD(this),
		   FIRST_CHILD(this)->fops->readdirp,
		   fd,
		   size,
		   offset,
		   xdata);
	dict_unref(xdata);
	return 0;
 error:
	STACK_UNWIND_STRICT(readdirp, frame, -1, ret, NULL, NULL);
	return 0;
}

static int32_t crypt_access(call_frame_t *frame,
			    xlator_t *this,
			    loc_t *loc,
			    int32_t mask, dict_t *xdata)
{
	gf_log(this->name, GF_LOG_WARNING,
	       "NFS mounts of encrypted volumes are unsupported");
	STACK_UNWIND_STRICT(access, frame, -1, EPERM, NULL);
	return 0;
}

int32_t master_set_block_size (xlator_t *this, crypt_private_t *priv,
			       dict_t *options)
{
	uint64_t block_size = 0;
	struct master_cipher_info *master = get_master_cinfo(priv);

	if (options != NULL)
		GF_OPTION_RECONF("block-size", block_size, options,
				 size, error);
	else
		GF_OPTION_INIT("block-size", block_size, size, error);

	switch (block_size) {
	case 512:
		master->m_block_bits = 9;
		break;
	case 1024:
		master->m_block_bits = 10;
		break;
	case 2048:
		master->m_block_bits = 11;
		break;
	case 4096:
		master->m_block_bits = 12;
		break;
	default:
		gf_log("crypt", GF_LOG_ERROR,
		       "FATAL: unsupported block size %llu",
		       (unsigned long long)block_size);
		goto error;
	}
	return 0;
 error:
	return -1;
}

int32_t master_set_alg(xlator_t *this, crypt_private_t *priv)
{
	struct master_cipher_info *master = get_master_cinfo(priv);
	master->m_alg = AES_CIPHER_ALG;
	return 0;
}

int32_t master_set_mode(xlator_t *this, crypt_private_t *priv)
{
	struct master_cipher_info *master = get_master_cinfo(priv);
	master->m_mode = XTS_CIPHER_MODE;
	return 0;
}

/*
 * set key size in bits to the master info
 * Pre-conditions: cipher mode in the master info is uptodate.
 */
static int master_set_data_key_size (xlator_t *this, crypt_private_t *priv,
				     dict_t *options)
{
	int32_t ret;
	uint64_t key_size = 0;
	struct master_cipher_info *master = get_master_cinfo(priv);

	if (options != NULL)
		GF_OPTION_RECONF("data-key-size", key_size, options,
				 size, error);
	else
		GF_OPTION_INIT("data-key-size", key_size, size, error);

	ret = data_cipher_algs[master->m_alg][master->m_mode].check_key(key_size);
	if (ret) {
		gf_log("crypt", GF_LOG_ERROR,
		       "FATAL: wrong bin key size %llu for alg %d mode %d",
		       (unsigned long long)key_size,
		       (int)master->m_alg,
		       (int)master->m_mode);
		goto error;
	}
	master->m_dkey_size = key_size;
	return 0;
 error:
	return -1;
}

static int is_hex(char *s) {
	return  ('0' <= *s && *s <= '9') || ('a' <= *s && *s <= 'f');
}

static int parse_hex_buf(xlator_t *this, char *src, unsigned char *dst,
			 int hex_size)
{
	int i;
	int hex_byte = 0;

	for (i = 0; i < (hex_size / 2); i++) {
		if (!is_hex(src + i*2) || !is_hex(src + i*2 + 1)) {
			gf_log("crypt", GF_LOG_ERROR,
			       "FATAL: not hex symbol in key");
			return -1;
		}
		if (sscanf(src + i*2, "%2x", &hex_byte) != 1) {
			gf_log("crypt", GF_LOG_ERROR,
			       "FATAL: can not parse hex key");
			return -1;
		}
		dst[i] = hex_byte & 0xff;
	}
	return 0;
}

/*
 * Parse options;
 * install master volume key
 */
int32_t master_set_master_vol_key(xlator_t *this, crypt_private_t *priv)
{
	int32_t ret;
	FILE *file = NULL;

	int32_t key_size;
	char *opt_key_file_pathname = NULL;

	unsigned char bin_buf[MASTER_VOL_KEY_SIZE];
	char hex_buf[2 * MASTER_VOL_KEY_SIZE];

	struct master_cipher_info *master = get_master_cinfo(priv);
	/*
	 * extract master key passed via option
	 */
	GF_OPTION_INIT("master-key", opt_key_file_pathname, path, bad_key);

	if (!opt_key_file_pathname) {
		gf_log(this->name, GF_LOG_ERROR, "FATAL: missing master key");
		return -1;
	}
	gf_log(this->name, GF_LOG_DEBUG, "handling file key %s",
	       opt_key_file_pathname);

	file = fopen(opt_key_file_pathname, "r");
	if (file == NULL) {
		gf_log(this->name, GF_LOG_ERROR,
		       "FATAL: can not open file with master key");
		return -1;
	}
	/*
	 * extract hex key
	 */
	key_size = fread(hex_buf, 1, sizeof(hex_buf), file);
	if (key_size < sizeof(hex_buf)) {
		gf_log(this->name, GF_LOG_ERROR,
		       "FATAL: master key is too short");
		goto bad_key;
	}
	ret = parse_hex_buf(this, hex_buf, bin_buf, key_size);
	if (ret)
		goto bad_key;
	memcpy(master->m_key, bin_buf, MASTER_VOL_KEY_SIZE);
	memset(hex_buf, 0, sizeof(hex_buf));
	fclose(file);

 	memset(bin_buf, 0, sizeof(bin_buf));
	return 0;
 bad_key:
	gf_log(this->name, GF_LOG_ERROR, "FATAL: bad master key");
	if (file)
		fclose(file);
	memset(bin_buf, 0, sizeof(bin_buf));
	return -1;
}

/*
 * Derive volume key for object-id authentication
 */
int32_t master_set_nmtd_vol_key(xlator_t *this, crypt_private_t *priv)
{
	return get_nmtd_vol_key(get_master_cinfo(priv));
}

int32_t crypt_init_xlator(xlator_t *this)
{
	int32_t ret;
	crypt_private_t *priv = this->private; 

	ret = master_set_alg(this, priv);
	if (ret)
		return ret;
	ret = master_set_mode(this, priv);
	if (ret)
		return ret;
	ret = master_set_block_size(this, priv, NULL);
	if (ret)
		return ret;
	ret = master_set_data_key_size(this, priv, NULL);
	if (ret)
		return ret;
	ret = master_set_master_vol_key(this, priv);
	if (ret)
		return ret;
	return master_set_nmtd_vol_key(this, priv);
}

static int32_t crypt_alloc_private(xlator_t *this)
{
	this->private = GF_CALLOC(1, sizeof(crypt_private_t), gf_crypt_mt_priv);
	if (!this->private) {
		gf_log("crypt", GF_LOG_ERROR,
		       "Can not allocate memory for private data");
		return ENOMEM;
	}
	return 0;
}

static void crypt_free_private(xlator_t *this)
{
	crypt_private_t *priv = this->private;
	if (priv) {
		memset(priv, 0, sizeof(*priv));
		GF_FREE(priv);
	}
}

int32_t reconfigure (xlator_t *this, dict_t *options)
{
	int32_t ret = -1;
	crypt_private_t *priv = NULL;

	GF_VALIDATE_OR_GOTO ("crypt", this, error);
	GF_VALIDATE_OR_GOTO (this->name, this->private, error);
	GF_VALIDATE_OR_GOTO (this->name, options, error);

	priv = this->private;

	ret = master_set_block_size(this, priv, options);
	if (ret) {
		gf_log("this->name", GF_LOG_ERROR,
		       "Failed to reconfure block size");
		goto error;
	}
	ret = master_set_data_key_size(this, priv, options);
	if (ret) {
		gf_log("this->name", GF_LOG_ERROR,
		       "Failed to reconfure data key size");
		goto error;
	}
	return 0;
 error:
	return ret;
}

int32_t init(xlator_t *this)
{
	int32_t ret;

	if (!this->children || this->children->next) {
		gf_log ("crypt", GF_LOG_ERROR,
			"FATAL: crypt should have exactly one child");
		return EINVAL;
	}
	if (!this->parents) {
		gf_log (this->name, GF_LOG_WARNING,
			"dangling volume. check volfile ");
	}
	ret = crypt_alloc_private(this);
	if (ret)
		return ret;
	ret = crypt_init_xlator(this);
	if (ret)
		goto error;
	this->local_pool = mem_pool_new(crypt_local_t, 64);
        if (!this->local_pool) {
		gf_log(this->name, GF_LOG_ERROR,
		       "failed to create local_t's memory pool");
		ret = ENOMEM;
                goto error;
        }
	gf_log ("crypt", GF_LOG_INFO, "crypt xlator loaded");
	return 0;
 error:
	crypt_free_private(this);
	return ret;
}

void fini (xlator_t *this)
{
	crypt_free_private(this);
}

struct xlator_fops fops = {
	.readv        = crypt_readv,
	.writev       = crypt_writev,
	.truncate     = crypt_truncate,
	.ftruncate    = crypt_ftruncate,
	.setxattr     = crypt_setxattr,
	.fsetxattr    = crypt_fsetxattr,
	.link         = crypt_link,
	.unlink       = crypt_unlink,
	.rename       = crypt_rename,
	.open         = crypt_open,
	.create       = crypt_create,
	.stat         = crypt_stat,
	.fstat        = crypt_fstat,
	.lookup       = crypt_lookup,
	.readdirp     = crypt_readdirp,
	.access       = crypt_access   
};

struct xlator_cbks cbks = {
	.forget       = crypt_forget
};

struct volume_options options[] = {
	{ .key = {"master-key"},
	  .type = GF_OPTION_TYPE_PATH,
	  .description = "Pathname of regular file which contains master volume key"
	},
	{ .key = {"data-key-size"},
	  .type = GF_OPTION_TYPE_SIZET,
	  .description = "Data key size (bits)",
	  .min = 256,
	  .max = 512,
	  .default_value = "256",
	},
	{ .key = {"block-size"},
	  .type = GF_OPTION_TYPE_SIZET,
	  .description = "Atom size (bits)",
	  .min = 512,
	  .max = 4096,
	  .default_value = "4096"
	},
	{ .key  = {NULL} },
};

/*
  Local variables:
  c-indentation-style: "K&R"
  mode-name: "LC"
  c-basic-offset: 8
  tab-width: 8
  fill-column: 80
  scroll-step: 1
  End:
*/
