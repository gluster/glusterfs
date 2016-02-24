/*
  Copyright (c) 2008-2013 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "defaults.h"
#include "crypt-common.h"
#include "crypt.h"

/*
 *                              Glossary
 *
 *
 * cblock            (or cipher block). A logical unit in a file.
 *                   cblock size is defined as the number of bits
 *                   in an input (or output) block of the block
 *                   cipher (*). Cipher block size is a property of
 *                   cipher algorithm. E.g. cblock size is 64 bits
 *                   for DES, 128 bits for AES, etc.
 *
 * atomic cipher     A cipher algorithm, which requires some chunks of
 * algorithm         text to be padded at left and(or) right sides before
 *                   cipher transaform.
 *
 *
 * block (atom)      Minimal chunk of file's data, which doesn't require
 *                   padding. We'll consider logical units in a file of
 *                   block size (atom size).
 *
 * cipher algorithm  Atomic cipher algorithm, which requires the last
 * with EOF issue    incomplete cblock in a file to be padded with some
 *                   data (usually zeros).
 *
 *
 * operation, which  reading/writing from offset, which is not aligned to
 * forms a gap at    to atom size
 * the beginning
 *
 *
 * operation, which  reading/writing count bytes starting from offset off,
 * forms a gap at    so that off+count is not aligned to atom_size
 * the end
 *
 * head block        the first atom affected by an operation, which forms
 *                   a gap at the beginning, or(and) at the end.
 *                   Сomment. Head block has at least one gap (either at
 *                   the beginning, or at the end)
 *
 *
 * tail block        the last atom different from head, affected by an
 *                   operation, which forms a gap at the end.
 *                   Сomment: Tail block has exactly one gap (at the end).
 *
 *
 * partial block     head or tail block
 *
 *
 * full block        block without gaps.
 *
 *
 * (*) Recommendation for Block Cipher Modes of Operation
 *     Methods and Techniques
 *     NIST Special Publication 800-38A Edition 2001
 */

/*
 * atom->offset_at()
 */
static off_t offset_at_head(struct avec_config *conf)
{
	return conf->aligned_offset;
}

static off_t offset_at_hole_head(call_frame_t *frame,
					 struct object_cipher_info *object)
{
	return offset_at_head(get_hole_conf(frame));
}

static off_t offset_at_data_head(call_frame_t *frame,
					 struct object_cipher_info *object)
{
	return offset_at_head(get_data_conf(frame));
}


static off_t offset_at_tail(struct avec_config *conf,
				    struct object_cipher_info *object)
{
	return conf->aligned_offset +
		(conf->off_in_head ? get_atom_size(object) : 0) +
		(conf->nr_full_blocks << get_atom_bits(object));
}

static off_t offset_at_hole_tail(call_frame_t *frame,
					 struct object_cipher_info *object)
{
	return offset_at_tail(get_hole_conf(frame), object);
}


static off_t offset_at_data_tail(call_frame_t *frame,
					 struct object_cipher_info *object)
{
	return offset_at_tail(get_data_conf(frame), object);
}

static off_t offset_at_full(struct avec_config *conf,
				    struct object_cipher_info *object)
{
	return conf->aligned_offset +
		(conf->off_in_head ? get_atom_size(object) : 0);
}

static off_t offset_at_data_full(call_frame_t *frame,
					 struct object_cipher_info *object)
{
	return offset_at_full(get_data_conf(frame), object);
}

static off_t offset_at_hole_full(call_frame_t *frame,
					 struct object_cipher_info *object)
{
	return offset_at_full(get_hole_conf(frame), object);
}

/*
 * atom->io_size_nopad()
 */

static uint32_t io_size_nopad_head(struct avec_config *conf,
				   struct object_cipher_info *object)
{
	uint32_t gap_at_beg;
	uint32_t gap_at_end;

	check_head_block(conf);

	gap_at_beg = conf->off_in_head;

	if (has_tail_block(conf) || has_full_blocks(conf) || conf->off_in_tail == 0 )
		gap_at_end = 0;
	else
		gap_at_end = get_atom_size(object) - conf->off_in_tail;

	return get_atom_size(object) - (gap_at_beg + gap_at_end);
}

static uint32_t io_size_nopad_tail(struct avec_config *conf,
				   struct object_cipher_info *object)
{
	check_tail_block(conf);
	return conf->off_in_tail;
}

static uint32_t io_size_nopad_full(struct avec_config *conf,
				   struct object_cipher_info *object)
{
	check_full_block(conf);
	return get_atom_size(object);
}

static uint32_t io_size_nopad_data_head(call_frame_t *frame,
					struct object_cipher_info *object)
{
	return io_size_nopad_head(get_data_conf(frame), object);
}

static uint32_t io_size_nopad_hole_head(call_frame_t *frame,
					struct object_cipher_info *object)
{
	return io_size_nopad_head(get_hole_conf(frame), object);
}

static uint32_t io_size_nopad_data_tail(call_frame_t *frame,
					struct object_cipher_info *object)
{
	return io_size_nopad_tail(get_data_conf(frame), object);
}

static uint32_t io_size_nopad_hole_tail(call_frame_t *frame,
					struct object_cipher_info *object)
{
	return io_size_nopad_tail(get_hole_conf(frame), object);
}

static uint32_t io_size_nopad_data_full(call_frame_t *frame,
					struct object_cipher_info *object)
{
	return io_size_nopad_full(get_data_conf(frame), object);
}

static uint32_t io_size_nopad_hole_full(call_frame_t *frame,
					struct object_cipher_info *object)
{
	return io_size_nopad_full(get_hole_conf(frame), object);
}

static uint32_t offset_in_head(struct avec_config *conf)
{
	check_cursor_head(conf);

	return conf->off_in_head;
}

static uint32_t offset_in_tail(call_frame_t *frame,
			       struct object_cipher_info *object)
{
	return 0;
}

static uint32_t offset_in_full(struct avec_config *conf,
			       struct object_cipher_info *object)
{
	check_cursor_full(conf);

	if (has_head_block(conf))
		return (conf->cursor - 1) << get_atom_bits(object);
	else
		return conf->cursor << get_atom_bits(object);
}

static uint32_t offset_in_data_head(call_frame_t *frame,
				    struct object_cipher_info *object)
{
	return offset_in_head(get_data_conf(frame));
}

static uint32_t offset_in_hole_head(call_frame_t *frame,
				    struct object_cipher_info *object)
{
	return offset_in_head(get_hole_conf(frame));
}

static uint32_t offset_in_data_full(call_frame_t *frame,
				    struct object_cipher_info *object)
{
	return offset_in_full(get_data_conf(frame), object);
}

static uint32_t offset_in_hole_full(call_frame_t *frame,
				    struct object_cipher_info *object)
{
	return offset_in_full(get_hole_conf(frame), object);
}

/*
 * atom->rmw()
 */
/*
 * Pre-conditions:
 * @vec contains plain text of the latest
 * version.
 *
 * Uptodate gaps of the @partial block with
 * this plain text, encrypt the whole block
 * and write the result to disk.
 */
static int32_t rmw_partial_block(call_frame_t *frame,
				 void *cookie,
				 xlator_t *this,
				 int32_t op_ret,
				 int32_t op_errno,
				 struct iovec *vec,
				 int32_t count,
				 struct iatt *stbuf,
				 struct iobref *iobref,
				 struct rmw_atom *atom)
{
	size_t was_read = 0;
	uint64_t file_size;
	crypt_local_t *local = frame->local;
	struct object_cipher_info *object = &local->info->cinfo;

	struct iovec *partial = atom->get_iovec(frame, 0);
	struct avec_config *conf = atom->get_config(frame);
	end_writeback_handler_t end_writeback_partial_block;
#if DEBUG_CRYPT
	gf_boolean_t check_last_cblock = _gf_false;
#endif
	local->op_ret = op_ret;
	local->op_errno = op_errno;

	if (op_ret < 0)
		goto exit;

	file_size = local->cur_file_size;
	was_read = op_ret;

	if (atom->locality == HEAD_ATOM && conf->off_in_head) {
		/*
		 * head atom with a non-uptodate gap
		 * at the beginning
		 *
		 * fill the gap with plain text of the
		 * latest version. Convert a part of hole
		 * (if any) to zeros.
		 */
		int32_t i;
		int32_t copied = 0;
		int32_t to_gap; /* amount of data needed to uptodate
				   the gap at the beginning */
#if 0
		int32_t hole = 0; /* The part of the hole which
				   * got in the head block */
#endif /* 0 */
		to_gap = conf->off_in_head;

		if (was_read < to_gap) {
			if (file_size >
			    offset_at_head(conf) + was_read) {
				/*
				 * It is impossible to uptodate
				 * head block: too few bytes have
				 * been read from disk, so that
				 * partial write is impossible.
				 *
				 * It could happen because of many
				 * reasons: IO errors, (meta)data
				 * corruption in the local file system,
				 * etc.
				 */
				gf_log(this->name, GF_LOG_WARNING,
				     "Can not uptodate a gap at the beginning");
				local->op_ret = -1;
				local->op_errno = EIO;
				goto exit;
			}
#if 0
			hole = to_gap - was_read;
#endif /* 0 */
			to_gap = was_read;
		}
		/*
		 * uptodate the gap at the beginning
		 */
		for (i = 0; i < count && copied < to_gap; i++) {
			int32_t to_copy;

			to_copy = vec[i].iov_len;
			if (to_copy > to_gap - copied)
				to_copy = to_gap - copied;

			memcpy(partial->iov_base, vec[i].iov_base, to_copy);
			copied += to_copy;
		}
#if 0
		/*
		 * If possible, convert part of the
		 * hole, which got in the head block
		 */
		ret = TRY_LOCK(&local->hole_lock);
		if (!ret) {
			if (local->hole_handled)
				/*
				 * already converted by
				 * crypt_writev_cbk()
				 */
				UNLOCK(&local->hole_lock);
			else {
				/*
				 * convert the part of the hole
				 * which got in the head block
				 * to zeros.
				 *
				 * Update the orig_offset to make
				 * sure writev_cbk() won't care
				 * about this part of the hole.
				 *
				 */
				memset(partial->iov_base + to_gap, 0, hole);

				conf->orig_offset -= hole;
				conf->orig_size += hole;
				UNLOCK(&local->hole_lock);
			}
		}
		else /*
		      * conversion is being performed
		      * by crypt_writev_cbk()
		      */
			;
#endif /* 0 */
	}
	if (atom->locality == TAIL_ATOM ||
	    (!has_tail_block(conf) && conf->off_in_tail)) {
		/*
		 * tail atom, or head atom with a non-uptodate
		 * gap at the end.
		 *
		 * fill the gap at the end of the block
		 * with plain text of the latest version.
		 * Pad the result, (if needed)
		 */
		int32_t i;
		int32_t to_gap;
		int copied;
		off_t off_in_tail;
		int32_t to_copy;

		off_in_tail = conf->off_in_tail;
		to_gap = conf->gap_in_tail;

		if (to_gap && was_read < off_in_tail + to_gap) {
			/*
			 * It is impossible to uptodate
			 * the gap at the end: too few bytes
			 * have been read from disk, so that
			 * partial write is impossible.
			 *
			 * It could happen because of many
			 * reasons: IO errors, (meta)data
			 * corruption in the local file system,
			 * etc.
			 */
			gf_log(this->name, GF_LOG_WARNING,
			       "Can not uptodate a gap at the end");
			local->op_ret = -1;
			local->op_errno = EIO;
			goto exit;
		}
		/*
		 * uptodate the gap at the end
		 */
		copied = 0;
		to_copy = to_gap;
		for(i = count - 1; i >= 0 && to_copy > 0; i--) {
			uint32_t from_vec, off_in_vec;

			off_in_vec = 0;
			from_vec = vec[i].iov_len;
			if (from_vec > to_copy) {
				off_in_vec = from_vec - to_copy;
				from_vec = to_copy;
			}
			memcpy(partial->iov_base +
			       off_in_tail + to_gap - copied - from_vec,
			       vec[i].iov_base + off_in_vec,
			       from_vec);

			gf_log(this->name, GF_LOG_DEBUG,
			       "uptodate %d bytes at tail. Offset at target(source): %d(%d)",
			       (int)from_vec,
			       (int)off_in_tail + to_gap - copied - from_vec,
			       (int)off_in_vec);

			copied += from_vec;
			to_copy -= from_vec;
		}
		partial->iov_len = off_in_tail + to_gap;

		if (object_alg_should_pad(object)) {
			int32_t resid = 0;
			resid = partial->iov_len & (object_alg_blksize(object) - 1);
			if (resid) {
				/*
				 * append a new EOF padding
				 */
				local->eof_padding_size =
					object_alg_blksize(object) - resid;

				gf_log(this->name, GF_LOG_DEBUG,
				       "set padding size %d",
				       local->eof_padding_size);

				memset(partial->iov_base + partial->iov_len,
				       1,
				       local->eof_padding_size);
				partial->iov_len += local->eof_padding_size;
#if DEBUG_CRYPT
				gf_log(this->name, GF_LOG_DEBUG,
				       "pad cblock with %d zeros:",
				       local->eof_padding_size);
				dump_cblock(this,
					    (unsigned char *)partial->iov_base +
					    partial->iov_len - object_alg_blksize(object));
				check_last_cblock = _gf_true;
#endif
			}
		}
	}
	/*
	 * encrypt the whole block
	 */
	encrypt_aligned_iov(object,
			    partial,
			    1,
			    atom->offset_at(frame, object));
#if DEBUG_CRYPT
	if (check_last_cblock == _gf_true) {
		gf_log(this->name, GF_LOG_DEBUG,
		       "encrypt last cblock with offset %llu",
		       (unsigned long long)atom->offset_at(frame, object));
		dump_cblock(this, (unsigned char *)partial->iov_base +
			    partial->iov_len - object_alg_blksize(object));
	}
#endif
	set_local_io_params_writev(frame, object, atom,
				   atom->offset_at(frame, object),
				   iovec_get_size(partial, 1));
	/*
	 * write the whole block to disk
	 */
	end_writeback_partial_block = dispatch_end_writeback(local->fop);
	conf->cursor ++;
	STACK_WIND(frame,
		   end_writeback_partial_block,
		   FIRST_CHILD(this),
		   FIRST_CHILD(this)->fops->writev,
		   local->fd,
		   partial,
		   1,
		   atom->offset_at(frame, object),
		   local->flags,
		   local->iobref_data,
		   local->xdata);

	gf_log("crypt", GF_LOG_DEBUG,
	       "submit partial block: %d bytes from %d offset",
	       (int)iovec_get_size(partial, 1),
	       (int)atom->offset_at(frame, object));
 exit:
	return 0;
}

/*
 * Perform a (read-)modify-write sequence.
 * This should be performed only after approval
 * of upper server-side manager, i.e. the caller
 * needs to make sure this is his turn to rmw.
 */
void submit_partial(call_frame_t *frame,
		    xlator_t *this,
		    fd_t *fd,
		    atom_locality_type ltype)
{
	int32_t ret;
	dict_t *dict;
	struct rmw_atom *atom;
	crypt_local_t *local = frame->local;
	struct object_cipher_info *object = &local->info->cinfo;

	atom = atom_by_types(local->active_setup, ltype);
	/*
	 * To perform the "read" component of the read-modify-write
	 * sequence the crypt translator does stack_wind to itself.
	 *
	 * Pass current file size to crypt_readv()
	 */
	dict = dict_new();
	if (!dict) {
		/*
		 * FIXME: Handle the error
		 */
		gf_log("crypt", GF_LOG_WARNING, "Can not alloc dict");
		return;
	}
	ret = dict_set(dict,
		       FSIZE_XATTR_PREFIX,
		       data_from_uint64(local->cur_file_size));
	if (ret) {
		/*
		 * FIXME: Handle the error
		 */
		dict_unref(dict);
		gf_log("crypt", GF_LOG_WARNING, "Can not set dict");
		goto exit;
	}
	STACK_WIND(frame,
		   atom->rmw,
		   this,
		   this->fops->readv, /* crypt_readv */
		   fd,
		   atom->count_to_uptodate(frame, object), /* count */
		   atom->offset_at(frame, object), /* offset to read from */
		   0,
		   dict);
 exit:
	dict_unref(dict);
}

/*
 * submit blocks of FULL_ATOM type
 */
void submit_full(call_frame_t *frame, xlator_t *this)
{
	crypt_local_t *local = frame->local;
	struct object_cipher_info *object = &local->info->cinfo;
	struct rmw_atom *atom = atom_by_types(local->active_setup, FULL_ATOM);
	uint32_t count; /* total number of full blocks to submit */
	uint32_t granularity; /* number of blocks to submit in one iteration */

	uint64_t off_in_file; /* start offset in the file, bytes */
	uint32_t off_in_atom; /* start offset in the atom, blocks */
	uint32_t blocks_written = 0; /* blocks written for this submit */

	struct avec_config *conf = atom->get_config(frame);
	end_writeback_handler_t end_writeback_full_block;
	/*
	 * Write full blocks by groups of granularity size.
	 */
	end_writeback_full_block = dispatch_end_writeback(local->fop);

	if (is_ordered_mode(frame)) {
		uint32_t skip = has_head_block(conf) ? 1 : 0;
		count = 1;
		granularity = 1;
		/*
		 * calculate start offset using cursor value;
		 * here we should take into accout head block,
		 * which corresponds to cursor value 0.
		 */
		off_in_file = atom->offset_at(frame, object) +
			((conf->cursor - skip) << get_atom_bits(object));
		off_in_atom = conf->cursor - skip;
	}
	else {
		/*
		 * in parallel mode
		 */
		count = conf->nr_full_blocks;
		granularity = MAX_IOVEC;
		off_in_file = atom->offset_at(frame, object);
		off_in_atom = 0;
	}
	while (count) {
		uint32_t blocks_to_write = count;

		if (blocks_to_write > granularity)
			blocks_to_write = granularity;
		if (conf->type == HOLE_ATOM)
			/*
			 * reset iovec before encryption
			 */
			memset(atom->get_iovec(frame, 0)->iov_base,
			       0,
			       get_atom_size(object));
		/*
		 * encrypt the group
		 */
		encrypt_aligned_iov(object,
				    atom->get_iovec(frame,
						    off_in_atom +
						    blocks_written),
				    blocks_to_write,
				    off_in_file + (blocks_written <<
						   get_atom_bits(object)));

		set_local_io_params_writev(frame, object, atom,
		        off_in_file + (blocks_written << get_atom_bits(object)),
			blocks_to_write <<  get_atom_bits(object));

		conf->cursor += blocks_to_write;

		STACK_WIND(frame,
			   end_writeback_full_block,
			   FIRST_CHILD(this),
			   FIRST_CHILD(this)->fops->writev,
			   local->fd,
			   atom->get_iovec(frame, off_in_atom + blocks_written),
			   blocks_to_write,
			   off_in_file + (blocks_written << get_atom_bits(object)),
			   local->flags,
			   local->iobref_data ? local->iobref_data : local->iobref,
			   local->xdata);

		gf_log("crypt", GF_LOG_DEBUG, "submit %d full blocks from %d offset",
		       blocks_to_write,
		       (int)(off_in_file + (blocks_written << get_atom_bits(object))));

		count -= blocks_to_write;
		blocks_written += blocks_to_write;
	}
	return;
}

static int32_t rmw_data_head(call_frame_t *frame,
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
	return rmw_partial_block(frame,
				 cookie,
				 this,
				 op_ret,
				 op_errno,
				 vec,
				 count,
				 stbuf,
				 iobref,
				 atom_by_types(DATA_ATOM, HEAD_ATOM));
}

static int32_t rmw_data_tail(call_frame_t *frame,
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
	return rmw_partial_block(frame,
				 cookie,
				 this,
				 op_ret,
				 op_errno,
				 vec,
				 count,
				 stbuf,
				 iobref,
				 atom_by_types(DATA_ATOM, TAIL_ATOM));
}

static int32_t rmw_hole_head(call_frame_t *frame,
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
	return rmw_partial_block(frame,
				 cookie,
				 this,
				 op_ret,
				 op_errno,
				 vec,
				 count,
				 stbuf,
				 iobref,
				 atom_by_types(HOLE_ATOM, HEAD_ATOM));
}

static int32_t rmw_hole_tail(call_frame_t *frame,
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
	return rmw_partial_block(frame,
				 cookie,
				 this,
				 op_ret,
				 op_errno,
				 vec,
				 count,
				 stbuf,
				 iobref,
				 atom_by_types(HOLE_ATOM, TAIL_ATOM));
}

/*
 * atom->count_to_uptodate()
 */
static uint32_t count_to_uptodate_head(struct avec_config *conf,
				       struct object_cipher_info *object)
{
	if (conf->acount == 1 && conf->off_in_tail)
		return get_atom_size(object);
	else
		/* there is no need to read the whole head block */
		return conf->off_in_head;
}

static uint32_t count_to_uptodate_tail(struct avec_config *conf,
				       struct object_cipher_info *object)
{
	/* we need to read the whole tail block */
	return get_atom_size(object);
}

static uint32_t count_to_uptodate_data_head(call_frame_t *frame,
					    struct object_cipher_info *object)
{
	return count_to_uptodate_head(get_data_conf(frame), object);
}

static uint32_t count_to_uptodate_data_tail(call_frame_t *frame,
					    struct object_cipher_info *object)
{
	return count_to_uptodate_tail(get_data_conf(frame), object);
}

static uint32_t count_to_uptodate_hole_head(call_frame_t *frame,
					    struct object_cipher_info *object)
{
	return count_to_uptodate_head(get_hole_conf(frame), object);
}

static uint32_t count_to_uptodate_hole_tail(call_frame_t *frame,
					    struct object_cipher_info *object)
{
	return count_to_uptodate_tail(get_hole_conf(frame), object);
}

/* atom->get_config() */

static struct avec_config *get_config_data(call_frame_t *frame)
{
	return &((crypt_local_t *)frame->local)->data_conf;
}

static struct avec_config *get_config_hole(call_frame_t *frame)
{
	return &((crypt_local_t *)frame->local)->hole_conf;
}

/*
 * atom->get_iovec()
 */
static struct iovec *get_iovec_hole_head(call_frame_t *frame,
					 uint32_t count)
{
	struct avec_config *conf = get_hole_conf(frame);

	return conf->avec;
}

static struct iovec *get_iovec_hole_full(call_frame_t *frame,
					 uint32_t count)
{
	struct avec_config *conf = get_hole_conf(frame);

	return conf->avec + (conf->off_in_head ? 1 : 0);
}

static struct iovec *get_iovec_hole_tail(call_frame_t *frame,
						uint32_t count)
{
	struct avec_config *conf = get_hole_conf(frame);

	return conf->avec + (conf->blocks_in_pool - 1);
}

static struct iovec *get_iovec_data_head(call_frame_t *frame,
						uint32_t count)
{
	struct avec_config *conf = get_data_conf(frame);

	return conf->avec;
}

static struct iovec *get_iovec_data_full(call_frame_t *frame,
						uint32_t count)
{
	struct avec_config *conf = get_data_conf(frame);

	return conf->avec + (conf->off_in_head ? 1 : 0) + count;
}

static struct iovec *get_iovec_data_tail(call_frame_t *frame,
						uint32_t count)
{
	struct avec_config *conf = get_data_conf(frame);

	return conf->avec +
		(conf->off_in_head ? 1 : 0) +
		conf->nr_full_blocks;
}

static struct rmw_atom atoms[LAST_DATA_TYPE][LAST_LOCALITY_TYPE] = {
	[DATA_ATOM][HEAD_ATOM] =
	{ .locality = HEAD_ATOM,
	  .rmw = rmw_data_head,
	  .offset_at = offset_at_data_head,
	  .offset_in = offset_in_data_head,
	  .get_iovec = get_iovec_data_head,
	  .io_size_nopad = io_size_nopad_data_head,
	  .count_to_uptodate = count_to_uptodate_data_head,
	  .get_config = get_config_data
	},
	[DATA_ATOM][TAIL_ATOM] =
	{ .locality = TAIL_ATOM,
	  .rmw = rmw_data_tail,
	  .offset_at = offset_at_data_tail,
	  .offset_in = offset_in_tail,
	  .get_iovec = get_iovec_data_tail,
	  .io_size_nopad = io_size_nopad_data_tail,
	  .count_to_uptodate = count_to_uptodate_data_tail,
	  .get_config = get_config_data
	},
	[DATA_ATOM][FULL_ATOM] =
	{ .locality = FULL_ATOM,
	  .offset_at = offset_at_data_full,
	  .offset_in = offset_in_data_full,
	  .get_iovec = get_iovec_data_full,
	  .io_size_nopad = io_size_nopad_data_full,
	  .get_config = get_config_data
	},
	[HOLE_ATOM][HEAD_ATOM] =
	{ .locality = HEAD_ATOM,
	  .rmw = rmw_hole_head,
	  .offset_at = offset_at_hole_head,
	  .offset_in = offset_in_hole_head,
	  .get_iovec = get_iovec_hole_head,
	  .io_size_nopad = io_size_nopad_hole_head,
	  .count_to_uptodate = count_to_uptodate_hole_head,
	  .get_config = get_config_hole
	},
	[HOLE_ATOM][TAIL_ATOM] =
	{ .locality = TAIL_ATOM,
	  .rmw = rmw_hole_tail,
	  .offset_at = offset_at_hole_tail,
	  .offset_in = offset_in_tail,
	  .get_iovec = get_iovec_hole_tail,
	  .io_size_nopad = io_size_nopad_hole_tail,
	  .count_to_uptodate = count_to_uptodate_hole_tail,
	  .get_config = get_config_hole
	},
	[HOLE_ATOM][FULL_ATOM] =
	{ .locality = FULL_ATOM,
	  .offset_at = offset_at_hole_full,
	  .offset_in = offset_in_hole_full,
	  .get_iovec = get_iovec_hole_full,
	  .io_size_nopad = io_size_nopad_hole_full,
	  .get_config = get_config_hole
	}
};

struct rmw_atom *atom_by_types(atom_data_type data,
			       atom_locality_type locality)
{
	return &atoms[data][locality];
}

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
