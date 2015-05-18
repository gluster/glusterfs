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

static void set_iv_aes_xts(off_t offset, struct object_cipher_info *object)
{
	unsigned char *ivec;

	ivec = object->u.aes_xts.ivec;

	/* convert the tweak into a little-endian byte
	 * array (IEEE P1619/D16, May 2007, section 5.1)
	 */

	*((uint64_t *)ivec) = htole64(offset);

	/* ivec is padded with zeroes */
}

static int32_t aes_set_keys_common(unsigned char *raw_key, uint32_t key_size,
				   AES_KEY *keys)
{
	int32_t ret;

	ret = AES_set_encrypt_key(raw_key,
				  key_size,
				  &keys[AES_ENCRYPT]);
	if (ret) {
		gf_log("crypt", GF_LOG_ERROR, "Set encrypt key failed");
		return ret;
	}
	ret = AES_set_decrypt_key(raw_key,
				  key_size,
				  &keys[AES_DECRYPT]);
	if (ret) {
		gf_log("crypt", GF_LOG_ERROR, "Set decrypt key failed");
		return ret;
	}
	return 0;
}

/*
 * set private cipher info for xts mode
 */
static int32_t set_private_aes_xts(struct crypt_inode_info *info,
				   struct master_cipher_info *master)
{
	int ret;
	struct object_cipher_info *object = get_object_cinfo(info);
	unsigned char *data_key;
	uint32_t subkey_size;

	/* init tweak value */
	memset(object->u.aes_xts.ivec, 0, 16);

	data_key = GF_CALLOC(1, object->o_dkey_size, gf_crypt_mt_key);
	if (!data_key)
		return ENOMEM;

	/*
	 * retrieve data keying meterial
	 */
	ret = get_data_file_key(info, master, object->o_dkey_size, data_key);
	if (ret) {
		gf_log("crypt", GF_LOG_ERROR, "Failed to retrieve data key");
		GF_FREE(data_key);
		return ret;
	}
	/*
	 * parse compound xts key
	 */
	subkey_size = object->o_dkey_size >> 4; /* (xts-key-size-in-bytes / 2) */
	/*
	 * install key for data encryption
	 */
	ret = aes_set_keys_common(data_key,
				  subkey_size << 3, object->u.aes_xts.dkey);
	if (ret) {
		GF_FREE(data_key);
		return ret;
	}
	/*
	 * set up key used to encrypt tweaks
	 */
	ret = AES_set_encrypt_key(data_key + subkey_size,
				  object->o_dkey_size / 2,
				  &object->u.aes_xts.tkey);
	if (ret < 0)
		gf_log("crypt", GF_LOG_ERROR, "Set tweak key failed");

	GF_FREE(data_key);
	return ret;
}

static int32_t aes_xts_init(void)
{
	cassert(AES_BLOCK_SIZE == (1 << AES_BLOCK_BITS));
	return 0;
}

static int32_t check_key_aes_xts(uint32_t keysize)
{
	switch(keysize) {
	case 256:
	case 512:
		return 0;
	default:
		break;
	}
	return -1;
}

static int32_t encrypt_aes_xts(const unsigned char *from,
			       unsigned char *to, size_t length,
			       off_t offset, const int enc,
			       struct object_cipher_info *object)
{
	XTS128_CONTEXT ctx;
	if (enc) {
		ctx.key1 = &object->u.aes_xts.dkey[AES_ENCRYPT];
		ctx.block1 = (block128_f)AES_encrypt;
	}
	else {
		ctx.key1 = &object->u.aes_xts.dkey[AES_DECRYPT];
		ctx.block1 = (block128_f)AES_decrypt;
	}
	ctx.key2 = &object->u.aes_xts.tkey;
	ctx.block2 = (block128_f)AES_encrypt;

	return CRYPTO_xts128_encrypt(&ctx,
				     object->u.aes_xts.ivec,
				     from,
				     to,
				     length, enc);
}

/*
 * Cipher input chunk @from of length @len;
 * @to: result of cipher transform;
 * @off: offset in a file (must be cblock-aligned);
 */
static void cipher_data(struct object_cipher_info *object,
			char *from,
			char *to,
			off_t off,
			size_t len,
			const int enc)
{
	crypt_check_input_len(len, object);

#if TRIVIAL_TFM && DEBUG_CRYPT
	return;
#endif
	data_cipher_algs[object->o_alg][object->o_mode].set_iv(off, object);
	data_cipher_algs[object->o_alg][object->o_mode].encrypt
		((const unsigned char *)from,
		 (unsigned char *)to,
		 len,
		 off,
		 enc,
		 object);
}

#define MAX_CIPHER_CHUNK (1 << 30)

/*
 * Do cipher (encryption/decryption) transform of a
 * continuous region of memory.
 *
 * @len: a number of bytes to transform;
 * @buf: data to transform;
 * @off: offset in a file, should be block-aligned
 *       for atomic cipher modes and ksize-aligned
 *       for other modes).
 * @dir: direction of transform (encrypt/decrypt).
 */
static void cipher_region(struct object_cipher_info *object,
			  char *from,
			  char *to,
			  off_t off,
			  size_t len,
			  int dir)
{
	while (len > 0) {
		size_t to_cipher;

		to_cipher = len;
		if (to_cipher > MAX_CIPHER_CHUNK)
			to_cipher = MAX_CIPHER_CHUNK;

		/* this will reset IV */
		cipher_data(object,
			    from,
			    to,
			    off,
			    to_cipher,
			    dir);
		from += to_cipher;
		to   += to_cipher;
		off  += to_cipher;
		len  -= to_cipher;
	}
}

/*
 * Do cipher transform (encryption/decryption) of
 * plaintext/ciphertext represented by @vec.
 *
 * Pre-conditions: @vec represents a continuous piece
 * of data in a file at offset @off to be ciphered
 * (encrypted/decrypted).
 * @count is the number of vec's components. All the
 * components must be block-aligned, the caller is
 * responsible for this. @dir is "direction" of
 * transform (encrypt/decrypt).
 */
static void cipher_aligned_iov(struct object_cipher_info *object,
			       struct iovec *vec,
			       int count,
			       off_t off,
			       int32_t dir)
{
	int i;
	int len = 0;

	for (i = 0; i < count; i++) {
		cipher_region(object,
			      vec[i].iov_base,
			      vec[i].iov_base,
			      off + len,
			      vec[i].iov_len,
			      dir);
		len += vec[i].iov_len;
	}
}

void encrypt_aligned_iov(struct object_cipher_info *object,
			 struct iovec *vec,
			 int count,
			 off_t off)
{
	cipher_aligned_iov(object, vec, count, off, 1);
}

void decrypt_aligned_iov(struct object_cipher_info *object,
			 struct iovec *vec,
			 int count,
			 off_t off)
{
	cipher_aligned_iov(object, vec, count, off, 0);
}

#if DEBUG_CRYPT
static void compound_stream(struct iovec *vec, int count, char *buf, off_t skip)
{
	int i;
	int off = 0;
	for (i = 0; i < count; i++) {
		memcpy(buf + off,
		       vec[i].iov_base + skip,
		       vec[i].iov_len - skip);

		off += (vec[i].iov_len - skip);
		skip = 0;
	}
}

static void check_iovecs(struct iovec *vec, int cnt,
			 struct iovec *avec, int acnt, uint32_t off_in_head)
{
	char *s1, *s2;
	uint32_t size, asize;

	size = iovec_get_size(vec, cnt);
	asize = iovec_get_size(avec, acnt) - off_in_head;
	if (size != asize) {
		gf_log("crypt", GF_LOG_DEBUG, "size %d is not eq asize %d",
		       size, asize);
		return;
	}
	s1 = GF_CALLOC(1, size, gf_crypt_mt_data);
	if (!s1) {
		gf_log("crypt", GF_LOG_DEBUG, "Can not allocate stream ");
		return;
	}
	s2 = GF_CALLOC(1, asize, gf_crypt_mt_data);
	if (!s2) {
		GF_FREE(s1);
		gf_log("crypt", GF_LOG_DEBUG, "Can not allocate stream ");
		return;
	}
	compound_stream(vec, cnt, s1, 0);
	compound_stream(avec, acnt, s2, off_in_head);
	if (memcmp(s1, s2, size))
		gf_log("crypt", GF_LOG_DEBUG, "chunks of different data");
	GF_FREE(s1);
	GF_FREE(s2);
}

#else
#define check_iovecs(vec, count, avec, avecn, off) noop
#endif /* DEBUG_CRYPT */

static char *data_alloc_block(xlator_t *this, crypt_local_t *local,
			      int32_t block_size)
{
	struct iobuf *iobuf = NULL;

	iobuf = iobuf_get2(this->ctx->iobuf_pool, block_size);
	if (!iobuf) {
		gf_log("crypt", GF_LOG_ERROR,
		       "Failed to get iobuf");
		return NULL;
	}
	if (!local->iobref_data) {
		local->iobref_data = iobref_new();
		if (!local->iobref_data) {
			gf_log("crypt", GF_LOG_ERROR,
			       "Failed to get iobref");
			iobuf_unref(iobuf);
			return NULL;
		}
	}
	iobref_add(local->iobref_data, iobuf);
	return iobuf->ptr;
}

/*
 * Compound @avec, which represent the same data
 * chunk as @vec, but has aligned components of
 * specified block size. Alloc blocks, if needed.
 * In particular, incomplete head and tail blocks
 * must be allocated.
 * Put number of allocated blocks to @num_blocks.
 *
 * Example:
 *
 * input: data chunk represented by 4 components
 * [AB],[BC],[CD],[DE];
 * output: 5 logical blocks (0, 1, 2, 3, 4).
 *
 *   A     B       C     D               E
 *   *-----*+------*-+---*----+--------+-*
 *   |     ||      | |   |    |        | |
 * *-+-----+*------+-*---+----*--------*-+------*
 * 0        1        2        3        4
 *
 * 0 - incomplete compound (head);
 * 1, 2 - full compound;
 * 3 - full non-compound (the case of reuse);
 * 4 - incomplete non-compound (tail).
 */
int32_t align_iov_by_atoms(xlator_t *this,
			   crypt_local_t *local,
			   struct object_cipher_info *object,
			   struct iovec *vec /* input vector */,
			   int32_t count /* number of vec components */,
			   struct iovec *avec /* aligned vector */,
			   char **blocks /* pool of blocks */,
			   uint32_t *blocks_allocated,
			   struct avec_config *conf)
{
	int vecn = 0;      /* number of the current component in vec */
	int avecn = 0;     /* number of the current component in avec */
	off_t vec_off = 0; /* offset in the current vec component,
			    * i.e. the number of bytes have already
			    * been copied */
	int32_t block_size = get_atom_size(object);
	size_t to_process; /* number of vec's bytes to copy and(or) re-use */
	int32_t off_in_head = conf->off_in_head;

	to_process = iovec_get_size(vec, count);

	while (to_process > 0) {
		if (off_in_head ||
		    vec[vecn].iov_len - vec_off < block_size) {
			/*
			 * less than block_size:
			 * the case of incomplete (head or tail),
			 * or compound block
			 */
			size_t copied = 0;
			/*
			 * populate the pool with a new block
			 */
			blocks[*blocks_allocated] = data_alloc_block(this,
								    local,
								    block_size);
			if (!blocks[*blocks_allocated])
				return -ENOMEM;
			memset(blocks[*blocks_allocated], 0, off_in_head);
			/*
			 * fill the block with vec components
			 */
			do {
				size_t to_copy;

				to_copy = vec[vecn].iov_len - vec_off;
				if (to_copy > block_size - off_in_head)
					to_copy = block_size - off_in_head;

				memcpy(blocks[*blocks_allocated] + off_in_head + copied,
				       vec[vecn].iov_base + vec_off,
				       to_copy);

				copied += to_copy;
				to_process -= to_copy;

				vec_off += to_copy;
				if (vec_off == vec[vecn].iov_len) {
					/* finished with this vecn */
					vec_off = 0;
					vecn++;
				}
			} while (copied < (block_size - off_in_head) && to_process > 0);
			/*
			 * update avec
			 */
			avec[avecn].iov_len = off_in_head + copied;
			avec[avecn].iov_base = blocks[*blocks_allocated];

			(*blocks_allocated)++;
			off_in_head = 0;
		} else {
			/*
			 * the rest of the current vec component
			 * is not less than block_size, so reuse
			 * the memory buffer of the component.
			 */
			size_t to_reuse;
			to_reuse = (to_process > block_size ?
				    block_size :
				    to_process);
			avec[avecn].iov_len = to_reuse;
			avec[avecn].iov_base = vec[vecn].iov_base + vec_off;

			vec_off += to_reuse;
			if (vec_off == vec[vecn].iov_len) {
				/* finished with this vecn */
				vec_off = 0;
				vecn++;
			}
			to_process -= to_reuse;
		}
		avecn++;
	}
	check_iovecs(vec, count, avec, avecn, conf->off_in_head);
	return 0;
}

/*
 * allocate and setup aligned vector for data submission
 * Pre-condition: @conf is set.
 */
int32_t set_config_avec_data(xlator_t *this,
			     crypt_local_t *local,
			     struct avec_config *conf,
			     struct object_cipher_info *object,
			     struct iovec *vec,
			     int32_t vec_count)
{
	int32_t ret = ENOMEM;
	struct iovec *avec;
	char **pool;
	uint32_t blocks_in_pool = 0;

	conf->type = DATA_ATOM;

	avec = GF_CALLOC(conf->acount, sizeof(*avec), gf_crypt_mt_iovec);
	if (!avec)
		return ret;
	pool = GF_CALLOC(conf->acount, sizeof(pool), gf_crypt_mt_char);
	if (!pool) {
		GF_FREE(avec);
		return ret;
	}
	if (!vec) {
		/*
		 * degenerated case: no data
		 */
		pool[0] = data_alloc_block(this, local, get_atom_size(object));
		if (!pool[0])
			goto free;
		blocks_in_pool = 1;
		avec->iov_base = pool[0];
		avec->iov_len = conf->off_in_tail;
	}
	else {
		ret = align_iov_by_atoms(this, local, object, vec, vec_count,
					 avec, pool, &blocks_in_pool, conf);
		if (ret)
			goto free;
	}
	conf->avec = avec;
	conf->pool = pool;
	conf->blocks_in_pool = blocks_in_pool;
	return 0;
 free:
	GF_FREE(avec);
	GF_FREE(pool);
	return ret;
}

/*
 * allocate and setup aligned vector for hole submission
 */
int32_t set_config_avec_hole(xlator_t *this,
			     crypt_local_t *local,
			     struct avec_config *conf,
			     struct object_cipher_info *object,
			     glusterfs_fop_t fop)
{
	uint32_t i, idx;
	struct iovec *avec;
	char **pool;
	uint32_t num_blocks;
	uint32_t blocks_in_pool = 0;

	conf->type = HOLE_ATOM;

	num_blocks = conf->acount -
		(conf->nr_full_blocks ? conf->nr_full_blocks - 1 : 0);

	switch (fop) {
	case GF_FOP_WRITE:
		/*
		 * hole goes before data
		 */
		if (num_blocks == 1 && conf->off_in_tail != 0)
		/*
		 * we won't submit a hole which fits into
		 * a data atom: this part of hole will be
		 * submitted with data write
		 */
			return 0;
		break;
	case GF_FOP_FTRUNCATE:
		/*
		 * expanding truncate, hole goes after data,
		 * and will be submited in any case.
 		 */
		break;
	default:
		gf_log("crypt", GF_LOG_WARNING,
		       "bad file operation %d", fop);
		return 0;
	}
	avec = GF_CALLOC(num_blocks, sizeof(*avec), gf_crypt_mt_iovec);
	if (!avec)
		return ENOMEM;
	pool = GF_CALLOC(num_blocks, sizeof(pool), gf_crypt_mt_char);
	if (!pool) {
		GF_FREE(avec);
		return ENOMEM;
	}
	for (i = 0; i < num_blocks; i++) {
		pool[i] = data_alloc_block(this, local, get_atom_size(object));
		if (pool[i] == NULL)
			goto free;
		blocks_in_pool++;
	}
	if (has_head_block(conf)) {
		/* set head block */
		idx = 0;
		avec[idx].iov_base = pool[idx];
		avec[idx].iov_len = get_atom_size(object);
		memset(avec[idx].iov_base + conf->off_in_head,
		       0,
		       get_atom_size(object) - conf->off_in_head);
	}
	if (has_tail_block(conf)) {
		/* set tail block */
		idx = num_blocks - 1;
		avec[idx].iov_base = pool[idx];
		avec[idx].iov_len = get_atom_size(object);
		memset(avec[idx].iov_base, 0, conf->off_in_tail);
	}
	if (has_full_blocks(conf)) {
		/* set full block */
		idx = conf->off_in_head ? 1 : 0;
		avec[idx].iov_base = pool[idx];
		avec[idx].iov_len = get_atom_size(object);
		/*
		 * since we re-use the buffer,
		 * zeroes will be set every time
		 * before encryption, see submit_full()
		 */
	}
	conf->avec = avec;
	conf->pool = pool;
	conf->blocks_in_pool = blocks_in_pool;
	return 0;
 free:
	GF_FREE(avec);
	GF_FREE(pool);
	return ENOMEM;
}

/* A helper for setting up config of partial atoms (which
 * participate in read-modify-write sequence).
 *
 * Calculate and setup precise amount of "extra-bytes"
 * that should be uptodated at the end of partial (not
 * necessarily tail!) block.
 *
 * Pre-condition: local->old_file_size is valid!
 * @conf contains setup, which is enough for correct calculation
 * of has_tail_block(), ->get_offset().
 */
void set_gap_at_end(call_frame_t *frame, struct object_cipher_info *object,
		    struct avec_config *conf, atom_data_type dtype)
{
	uint32_t to_block;
	crypt_local_t *local = frame->local;
	uint64_t old_file_size = local->old_file_size;
	struct rmw_atom *partial = atom_by_types(dtype,
						 has_tail_block(conf) ?
						 TAIL_ATOM : HEAD_ATOM);

	if (old_file_size <= partial->offset_at(frame, object))
		to_block = 0;
	else {
		to_block = old_file_size - partial->offset_at(frame, object);
		if (to_block > get_atom_size(object))
			to_block = get_atom_size(object);
	}
	if (to_block > conf->off_in_tail)
		conf->gap_in_tail = to_block - conf->off_in_tail;
	else
		/*
		 * nothing to uptodate
		 */
		conf->gap_in_tail = 0;
}

/*
 * fill struct avec_config with offsets layouts
 */
void set_config_offsets(call_frame_t *frame,
			xlator_t *this,
			uint64_t offset,
			uint64_t count,
			atom_data_type dtype,
			int32_t set_gap)
{
	crypt_local_t *local;
	struct object_cipher_info *object;
	struct avec_config *conf;
	uint32_t resid;

	uint32_t atom_size;
	uint32_t atom_bits;

	size_t orig_size;
	off_t orig_offset;
	size_t expanded_size;
	off_t aligned_offset;

	uint32_t off_in_head = 0;
	uint32_t off_in_tail = 0;
	uint32_t nr_full_blocks;
	int32_t size_full_blocks;

	uint32_t acount; /* number of alifned components to write.
			  * The same as number of occupied logical
			  * blocks (atoms)
			  */
	local = frame->local;
	object = &local->info->cinfo;
	conf = (dtype == DATA_ATOM ?
		get_data_conf(frame) : get_hole_conf(frame));

	orig_offset = offset;
	orig_size = count;

	atom_size = get_atom_size(object);
	atom_bits = get_atom_bits(object);

	/*
	 * Round-down the start,
	 * round-up the end.
	 */
	resid = offset & (uint64_t)(atom_size - 1);

	if (resid)
		off_in_head = resid;
	aligned_offset = offset - off_in_head;
	expanded_size = orig_size + off_in_head;

	/* calculate tail,
	   expand size forward  */
	resid = (offset + orig_size) & (uint64_t)(atom_size - 1);

	if (resid) {
		off_in_tail = resid;
		expanded_size += (atom_size - off_in_tail);
	}
	/*
	 * calculate number of occupied blocks
	 */
	acount = expanded_size >> atom_bits;
	/*
	 * calculate number of full blocks
	 */
	size_full_blocks = expanded_size;
	if (off_in_head)
		size_full_blocks -= atom_size;
	if (off_in_tail && size_full_blocks > 0)
		size_full_blocks -= atom_size;
	nr_full_blocks = size_full_blocks >> atom_bits;

	conf->atom_size = atom_size;
	conf->orig_size = orig_size;
	conf->orig_offset = orig_offset;
	conf->expanded_size = expanded_size;
	conf->aligned_offset = aligned_offset;

	conf->off_in_head = off_in_head;
	conf->off_in_tail = off_in_tail;
	conf->nr_full_blocks = nr_full_blocks;
	conf->acount = acount;
	/*
	 * Finally, calculate precise amount of
	 * "extra-bytes" that should be uptodated
	 * at the end.
	 * Only if RMW is expected.
	 */
	if (off_in_tail && set_gap)
		set_gap_at_end(frame, object, conf, dtype);
}

struct data_cipher_alg data_cipher_algs[LAST_CIPHER_ALG][LAST_CIPHER_MODE] = {
	[AES_CIPHER_ALG][XTS_CIPHER_MODE] =
	{ .atomic = _gf_true,
	  .should_pad = _gf_true,
	  .blkbits = AES_BLOCK_BITS,
	  .init = aes_xts_init,
	  .set_private = set_private_aes_xts,
	  .check_key = check_key_aes_xts,
	  .set_iv = set_iv_aes_xts,
	  .encrypt = encrypt_aes_xts
	}
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
