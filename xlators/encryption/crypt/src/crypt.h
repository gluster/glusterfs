/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __CRYPT_H__
#define __CRYPT_H__

#include <openssl/aes.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <openssl/cmac.h>
#include <openssl/modes.h>
#include "crypt-mem-types.h"
#include "compat.h"

#define CRYPT_XLATOR_ID  (0)

#define MAX_IOVEC_BITS (3)
#define MAX_IOVEC (1 << MAX_IOVEC_BITS)
#define KEY_FACTOR_BITS  (6)

#define DEBUG_CRYPT (0)
#define TRIVIAL_TFM (0)

#define CRYPT_MIN_BLOCK_BITS (9)
#define CRYPT_MAX_BLOCK_BITS (12)

#define MASTER_VOL_KEY_SIZE (32)
#define NMTD_VOL_KEY_SIZE (16)

#if !defined(GF_LINUX_HOST_OS)
typedef off_t loff_t;
#endif

struct crypt_key {
	uint32_t len;
	const char *label;
};

/*
 * Add new key types to the end of this
 * enumeration but before LAST_KEY_TYPE
 */
typedef enum {
	MASTER_VOL_KEY,
	NMTD_VOL_KEY,
	NMTD_LINK_KEY,
	EMTD_FILE_KEY,
	DATA_FILE_KEY_256,
	DATA_FILE_KEY_512,
	LAST_KEY_TYPE
}crypt_key_type;

struct kderive_context {
	const unsigned char *pkey;/* parent key */
	uint32_t pkey_len;        /* parent key size, bits */
	uint32_t ckey_len;        /* child key size, bits */
	unsigned char *fid;       /* fixed input data, NIST 800-108, 5.1 */
	uint32_t fid_len;         /* fid len, bytes */
	unsigned char *out;       /* contains child keying material */
	uint32_t out_len;         /* out len, bytes */
};

typedef enum {
	DATA_ATOM,
	HOLE_ATOM,
	LAST_DATA_TYPE
}atom_data_type;

typedef enum {
	HEAD_ATOM,
	TAIL_ATOM,
	FULL_ATOM,
	LAST_LOCALITY_TYPE
}atom_locality_type;

typedef enum {
	MTD_CREATE,
	MTD_APPEND,
	MTD_OVERWRITE,
	MTD_CUT,
	MTD_LAST_OP
} mtd_op_t;

struct xts128_context {
	void      *key1, *key2;
	block128_f block1,block2;
};

struct object_cipher_info {
	cipher_alg_t  o_alg;
        cipher_mode_t o_mode;
	uint32_t      o_block_bits;
        uint32_t      o_dkey_size; /* raw data key size in bits */
	union {
		struct {
			unsigned char ivec[16];
			AES_KEY dkey[2];
			AES_KEY tkey;    /* key used for tweaking */
			XTS128_CONTEXT xts;
		} aes_xts;
	} u;
};

struct master_cipher_info {
	/*
	 * attributes inherited by newly created regular files
	 */
	cipher_alg_t  m_alg;
        cipher_mode_t m_mode;
	uint32_t      m_block_bits;
        uint32_t      m_dkey_size; /* raw key size in bits */
	/*
	 * master key
	 */
	unsigned char m_key[MASTER_VOL_KEY_SIZE];
	/*
	 * volume key for oid authentication
	 */
	unsigned char m_nmtd_key[NMTD_VOL_KEY_SIZE];
};

/*
* This info is not changed during file's life
 */
struct crypt_inode_info {
#if DEBUG_CRYPT
	loc_t *loc; /* pathname that the file has been
		       opened, or created with */
#endif
	uint16_t nr_minor;
	uuid_t oid;
	struct object_cipher_info cinfo;
};

/*
 * this should locate in secure memory
 */
typedef struct {
	struct master_cipher_info master;
} crypt_private_t;

static inline struct master_cipher_info *get_master_cinfo(crypt_private_t *priv)
{
	return &priv->master;
}

static inline struct object_cipher_info *get_object_cinfo(struct crypt_inode_info
							 *info)
{
	return &info->cinfo;
}

/*
 * this describes layouts and properties
 * of atoms in an aligned vector
 */
struct avec_config {
	uint32_t atom_size;
	atom_data_type type;
	size_t orig_size;
	off_t orig_offset;
	size_t expanded_size;
	off_t aligned_offset;

	uint32_t off_in_head;
	uint32_t off_in_tail;
	uint32_t gap_in_tail;
	uint32_t nr_full_blocks;

	struct iovec *avec; /* aligned vector */
	uint32_t acount; /* number of avec components. The same
			  * as number of occupied logical blocks */
	char **pool;
	uint32_t blocks_in_pool;
	uint32_t cursor; /* makes sense only for ordered writes,
			  * so there is no races on this counter.
			  *
			  * Cursor is per-config object, we don't
			  * reset cursor for atoms of different
			  * localities (head, tail, full)
			  */
};


typedef struct {
	glusterfs_fop_t fop; /* code of FOP this local info built for */
	fd_t *fd;
	inode_t *inode;
	loc_t *loc;
	int32_t mac_idx;
	loc_t *newloc;
	int32_t flags;
	int32_t wbflags;
	struct crypt_inode_info *info;
	struct iobref *iobref;
	struct iobref *iobref_data;
	off_t offset;

	uint64_t old_file_size; /* per FOP, retrieved under lock held */
	uint64_t cur_file_size; /* per iteration, before issuing IOs */
	uint64_t new_file_size; /* per iteration, after issuing IOs */

	uint64_t io_offset; /* offset of IOs issued per iteration */
	uint64_t io_offset_nopad; /* offset of user's data in the atom */
	uint32_t io_size; /* size of IOs issued per iteration */
	uint32_t io_size_nopad; /* size of user's data in the IOs */
	uint32_t eof_padding_size; /* size od EOF padding in the IOs */

	gf_lock_t call_lock; /* protect nr_calls from many cbks */
	int32_t nr_calls;

	atom_data_type active_setup; /* which setup (hole or date)
					is currently active */
	/* data setup */
	struct avec_config data_conf;

	/* hole setup */
	int hole_conv_in_proggress;
	gf_lock_t hole_lock; /* protect hole config from many cbks */
	int hole_handled;
	struct avec_config hole_conf;
	struct iatt buf;
	struct iatt prebuf;
	struct iatt postbuf;
	struct iatt *prenewparent;
	struct iatt *postnewparent;
	int32_t op_ret;
	int32_t op_errno;
	int32_t rw_count; /* total read or written */
	gf_lock_t rw_count_lock; /* protect the counter above */
	unsigned char *format; /* for create, update format string */
	uint32_t format_size;
	uint32_t msgflags; /* messages for crypt_open() */
	dict_t *xdata;
	dict_t *xattr;
	struct iovec vec; /* contains last file's atom for
			     read-prune-write sequence */
	gf_boolean_t custom_mtd;
	/*
	 * the next 3 fields are used by readdir and friends
	 */
	gf_dirent_t *de; /* directory entry */
	char *de_path; /* pathname of directory entry */
	uint32_t de_prefix_len; /* length of the parent's pathname */
	gf_dirent_t *entries;

	uint32_t update_disk_file_size:1;
} crypt_local_t;

/* This represents a (read)modify-write atom */
struct rmw_atom {
	atom_locality_type locality;
	/*
	 * read-modify-write sequence of the atom
	 */
	int32_t (*rmw)(call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno,
		       struct iovec *vec,
		       int32_t count,
		       struct iatt *stbuf,
		       struct iobref *iobref,
		       dict_t *xdata);
	/*
	 * offset of the logical block in a file
	 */
	loff_t (*offset_at)(call_frame_t *frame,
			    struct object_cipher_info *object);
	/*
	 * IO offset in an atom
	 */
	uint32_t (*offset_in)(call_frame_t *frame,
			      struct object_cipher_info *object);
	/*
	 * number of bytes of plain text of this atom that user
	 * wants to read/write.
	 * It can be smaller than atom_size in the case of head
	 * or tail atoms.
	 */
	uint32_t (*io_size_nopad)(call_frame_t *frame,
				  struct object_cipher_info *object);
	/*
	 * which iovec represents the atom
	 */
	struct iovec *(*get_iovec)(call_frame_t *frame, uint32_t count);
	/*
	 * how many bytes of partial block should be uptodated by
	 * reading from disk.
	 * This is used to perform a read component of RMW (read-modify-write).
	 */
	uint32_t (*count_to_uptodate)(call_frame_t *frame, struct object_cipher_info *object);
	struct avec_config *(*get_config)(call_frame_t *frame);
};

struct data_cipher_alg {
	gf_boolean_t atomic; /* true means that algorithm requires
				to pad data before cipher transform */
	gf_boolean_t should_pad; /* true means that algorithm requires
				    to pad the end of file with extra-data */
	uint32_t blkbits; /* blksize = 1 << blkbits */
	/*
	 * any preliminary sanity checks goes here
	 */
	int32_t (*init)(void);
	/*
	 * set alg-mode specific inode info
	 */
	int32_t (*set_private)(struct crypt_inode_info *info,
			       struct master_cipher_info *master);
	/*
	 * check alg-mode specific data key
	 */
	int32_t (*check_key)(uint32_t key_size);
	void (*set_iv)(off_t offset, struct object_cipher_info *object);
	int32_t (*encrypt)(const unsigned char *from, unsigned char *to,
			   size_t length, off_t offset, const int enc,
			   struct object_cipher_info *object);
};

/*
 * version-dependent metadata loader
 */
struct crypt_mtd_loader {
	/*
	 * return core format size
	 */
	size_t (*format_size)(mtd_op_t op, size_t old_size);
	/*
	 * pack version-specific metadata of an object
	 * at ->create()
	 */
	int32_t (*create_format)(unsigned char *wire,
				 loc_t *loc,
				 struct crypt_inode_info *info,
				 struct master_cipher_info *master);
	/*
	 * extract version-specific metadata of an object
	 * at ->open() time
	 */
	int32_t (*open_format)(unsigned char *wire,
			       int32_t len,
			       loc_t *loc,
			       struct crypt_inode_info *info,
			       struct master_cipher_info *master,
			       crypt_local_t *local,
			       gf_boolean_t load_info);
	int32_t (*update_format)(unsigned char *new,
				 unsigned char *old,
				 size_t old_len,
				 int32_t mac_idx,
				 mtd_op_t op,
				 loc_t *loc,
				 struct crypt_inode_info *info,
				 struct master_cipher_info *master,
				 crypt_local_t *local);
};

typedef int32_t (*end_writeback_handler_t)(call_frame_t *frame,
					   void *cookie,
					   xlator_t *this,
					   int32_t op_ret,
					   int32_t op_errno,
					   struct iatt *prebuf,
					   struct iatt *postbuf,
					   dict_t *xdata);
typedef void (*linkop_wind_handler_t)(call_frame_t *frame, xlator_t *this);
typedef void (*linkop_unwind_handler_t)(call_frame_t *frame);


/* Declarations */

/* keys.c */
extern struct crypt_key crypt_keys[LAST_KEY_TYPE];
int32_t get_nmtd_vol_key(struct master_cipher_info *master);
int32_t get_nmtd_link_key(loc_t *loc,
			  struct master_cipher_info *master,
			  unsigned char *result);
int32_t get_emtd_file_key(struct crypt_inode_info *info,
			  struct master_cipher_info *master,
			  unsigned char *result);
int32_t get_data_file_key(struct crypt_inode_info *info,
			  struct master_cipher_info *master,
			  uint32_t keysize,
			  unsigned char *key);
/* data.c */
extern struct data_cipher_alg data_cipher_algs[LAST_CIPHER_ALG][LAST_CIPHER_MODE];
void encrypt_aligned_iov(struct object_cipher_info *object,
			 struct iovec *vec,
			 int count,
			 off_t off);
void decrypt_aligned_iov(struct object_cipher_info *object,
			 struct iovec *vec,
			 int count,
			 off_t off);
int32_t align_iov_by_atoms(xlator_t *this,
			   crypt_local_t *local,
			   struct object_cipher_info *object,
			   struct iovec *vec /* input vector */,
			   int32_t count /* number of vec components */,
			   struct iovec *avec /* aligned vector */,
			   char **blocks /* pool of blocks */,
			   uint32_t *blocks_allocated,
			   struct avec_config *conf);
int32_t set_config_avec_data(xlator_t *this,
			     crypt_local_t *local,
			     struct avec_config *conf,
			     struct object_cipher_info *object,
			     struct iovec *vec,
			     int32_t vec_count);
int32_t set_config_avec_hole(xlator_t *this,
			     crypt_local_t *local,
			     struct avec_config *conf,
			     struct object_cipher_info *object,
			     glusterfs_fop_t fop);
void set_gap_at_end(call_frame_t *frame, struct object_cipher_info *object,
		    struct avec_config *conf, atom_data_type dtype);
void set_config_offsets(call_frame_t *frame,
			xlator_t *this,
			uint64_t offset,
			uint64_t count,
			atom_data_type dtype,
			int32_t setup_gap_in_tail);

/* metadata.c */
extern struct crypt_mtd_loader mtd_loaders [LAST_MTD_LOADER];

int32_t alloc_format(crypt_local_t *local, size_t size);
int32_t alloc_format_create(crypt_local_t *local);
void free_format(crypt_local_t *local);
size_t format_size(mtd_op_t op, size_t old_size);
size_t new_format_size(void);
int32_t open_format(unsigned char *str, int32_t len, loc_t *loc,
		    struct crypt_inode_info *info,
		    struct master_cipher_info *master, crypt_local_t *local,
		    gf_boolean_t load_info);
int32_t update_format(unsigned char *new, unsigned char *old,
		      size_t old_len, int32_t mac_idx, mtd_op_t op, loc_t *loc,
		      struct crypt_inode_info *info,
		      struct master_cipher_info *master,
		      crypt_local_t *local);
int32_t create_format(unsigned char *wire,
		      loc_t *loc,
		      struct crypt_inode_info *info,
		      struct master_cipher_info *master);

/* atom.c */
struct rmw_atom *atom_by_types(atom_data_type data,
			       atom_locality_type locality);
void submit_partial(call_frame_t *frame,
		    xlator_t *this,
		    fd_t *fd,
		    atom_locality_type ltype);
void submit_full(call_frame_t *frame, xlator_t *this);

/* crypt.c */

end_writeback_handler_t dispatch_end_writeback(glusterfs_fop_t fop);
static size_t iovec_get_size(struct iovec *vec, uint32_t count);
void set_local_io_params_writev(call_frame_t *frame,
				struct object_cipher_info *object,
				struct rmw_atom *atom, off_t io_offset,
				uint32_t io_size);
void link_wind(call_frame_t *frame, xlator_t *this);
void unlink_wind(call_frame_t *frame, xlator_t *this);
void link_unwind(call_frame_t *frame);
void unlink_unwind(call_frame_t *frame);
void rename_wind(call_frame_t *frame, xlator_t *this);
void rename_unwind(call_frame_t *frame);

/* Inline functions */

static inline size_t iovec_get_size(struct iovec *vec, uint32_t count)
{
	int i;
	size_t size = 0;
	for (i = 0; i < count; i++)
		size += vec[i].iov_len;
	return size;
}

static inline int32_t crypt_xlator_id(void)
{
	return CRYPT_XLATOR_ID;
}

static inline mtd_loader_id current_mtd_loader(void)
{
	return MTD_LOADER_V1;
}

static inline uint32_t master_key_size (void)
{
	return crypt_keys[MASTER_VOL_KEY].len >> 3;
}

static inline uint32_t nmtd_vol_key_size (void)
{
	return crypt_keys[NMTD_VOL_KEY].len >> 3;
}

static inline uint32_t alg_mode_blkbits(cipher_alg_t alg,
					cipher_mode_t mode)
{
	return data_cipher_algs[alg][mode].blkbits;
}

static inline uint32_t alg_mode_blksize(cipher_alg_t alg,
					cipher_mode_t mode)
{
	return 1 << alg_mode_blkbits(alg, mode);
}

static inline gf_boolean_t alg_mode_atomic(cipher_alg_t alg,
					   cipher_mode_t mode)
{
	return data_cipher_algs[alg][mode].atomic;
}

static inline gf_boolean_t alg_mode_should_pad(cipher_alg_t alg,
					       cipher_mode_t mode)
{
	return data_cipher_algs[alg][mode].should_pad;
}

static inline uint32_t master_alg_blksize(struct master_cipher_info *mr)
{
	return alg_mode_blksize(mr->m_alg, mr->m_mode);
}

static inline uint32_t master_alg_blkbits(struct master_cipher_info *mr)
{
	return alg_mode_blkbits(mr->m_alg, mr->m_mode);
}

static inline gf_boolean_t master_alg_atomic(struct master_cipher_info *mr)
{
	return alg_mode_atomic(mr->m_alg, mr->m_mode);
}

static inline gf_boolean_t master_alg_should_pad(struct master_cipher_info *mr)
{
	return alg_mode_should_pad(mr->m_alg, mr->m_mode);
}

static inline uint32_t object_alg_blksize(struct object_cipher_info *ob)
{
	return alg_mode_blksize(ob->o_alg, ob->o_mode);
}

static inline uint32_t object_alg_blkbits(struct object_cipher_info *ob)
{
	return alg_mode_blkbits(ob->o_alg, ob->o_mode);
}

static inline gf_boolean_t object_alg_atomic(struct object_cipher_info *ob)
{
	return alg_mode_atomic(ob->o_alg, ob->o_mode);
}

static inline gf_boolean_t object_alg_should_pad(struct object_cipher_info *ob)
{
	return alg_mode_should_pad(ob->o_alg, ob->o_mode);
}

static inline uint32_t aes_raw_key_size(struct master_cipher_info *master)
{
	return master->m_dkey_size >> 3;
}

static inline struct avec_config *get_hole_conf(call_frame_t *frame)
{
	return &(((crypt_local_t *)frame->local)->hole_conf);
}

static inline struct avec_config *get_data_conf(call_frame_t *frame)
{
	return &(((crypt_local_t *)frame->local)->data_conf);
}

static inline int32_t get_atom_bits (struct object_cipher_info *object)
{
	return object->o_block_bits;
}

static inline int32_t get_atom_size (struct object_cipher_info *object)
{
	return 1 << get_atom_bits(object);
}

static inline int32_t has_head_block(struct avec_config *conf)
{
	return conf->off_in_head ||
		(conf->acount == 1 && conf->off_in_tail);
}

static inline int32_t has_tail_block(struct avec_config *conf)
{
	return conf->off_in_tail && conf->acount > 1;
}

static inline int32_t has_full_blocks(struct avec_config *conf)
{
	return conf->nr_full_blocks;
}

static inline int32_t should_submit_head_block(struct avec_config *conf)
{
	return has_head_block(conf) && (conf->cursor == 0);
}

static inline int32_t should_submit_tail_block(struct avec_config *conf)
{
	return has_tail_block(conf) && (conf->cursor == conf->acount - 1);
}

static inline int32_t should_submit_full_block(struct avec_config *conf)
{
	uint32_t start = has_head_block(conf) ? 1 : 0;

	return has_full_blocks(conf) &&
		conf->cursor >= start &&
		conf->cursor < start + conf->nr_full_blocks;
}

#if DEBUG_CRYPT
static inline void crypt_check_input_len(size_t len,
					 struct object_cipher_info *object)
{
	if (object_alg_should_pad(object) && (len & (object_alg_blksize(object) - 1)))
		gf_log ("crypt", GF_LOG_DEBUG, "bad input len: %d", (int)len);
}

static inline void check_head_block(struct avec_config *conf)
{
	if (!has_head_block(conf))
		gf_log("crypt", GF_LOG_DEBUG, "not a head atom");
}

static inline void check_tail_block(struct avec_config *conf)
{
	if (!has_tail_block(conf))
		gf_log("crypt", GF_LOG_DEBUG, "not a tail atom");
}

static inline void check_full_block(struct avec_config *conf)
{
	if (!has_full_blocks(conf))
		gf_log("crypt", GF_LOG_DEBUG, "not a full atom");
}

static inline void check_cursor_head(struct avec_config *conf)
{
	if (!has_head_block(conf))
		gf_log("crypt",
		       GF_LOG_DEBUG, "Illegal call of head atom method");
	else if (conf->cursor != 0)
		gf_log("crypt",
		       GF_LOG_DEBUG, "Cursor (%d) is not at head atom",
		       conf->cursor);
}

static inline void check_cursor_full(struct avec_config *conf)
{
	if (!has_full_blocks(conf))
		gf_log("crypt",
		       GF_LOG_DEBUG, "Illegal call of full atom method");
	if (has_head_block(conf) && (conf->cursor == 0))
		gf_log("crypt",
		       GF_LOG_DEBUG, "Cursor is not at full atom");
}

/*
 * FIXME: use avec->iov_len to check setup
 */
static inline int data_local_invariant(crypt_local_t *local)
{
	return 0;
}

#else
#define crypt_check_input_len(len, object) noop
#define check_head_block(conf) noop
#define check_tail_block(conf) noop
#define check_full_block(conf) noop
#define check_cursor_head(conf) noop
#define check_cursor_full(conf) noop

#endif /* DEBUG_CRYPT */

static inline struct avec_config *conf_by_type(call_frame_t *frame,
					       atom_data_type dtype)
{
	struct avec_config *conf = NULL;

	switch (dtype) {
	case HOLE_ATOM:
		conf = get_hole_conf(frame);
		break;
	case DATA_ATOM:
		conf = get_data_conf(frame);
		break;
	default:
		gf_log("crypt", GF_LOG_DEBUG, "bad atom type");
	}
	return conf;
}

static inline uint32_t nr_calls_head(struct avec_config *conf)
{
	return has_head_block(conf) ? 1 : 0;
}

static inline uint32_t nr_calls_tail(struct avec_config *conf)
{
	return has_tail_block(conf) ? 1 : 0;
}

static inline uint32_t nr_calls_full(struct avec_config *conf)
{
	switch(conf->type) {
	case HOLE_ATOM:
		return has_full_blocks(conf);
	case DATA_ATOM:
		return has_full_blocks(conf) ?
			logical_blocks_occupied(0,
						conf->nr_full_blocks,
						MAX_IOVEC_BITS)	: 0;
	default:
		gf_log("crypt", GF_LOG_DEBUG, "bad atom data type");
		return 0;
	}
}

static inline uint32_t nr_calls(struct avec_config *conf)
{
	return nr_calls_head(conf) + nr_calls_tail(conf) + nr_calls_full(conf);
}

static inline uint32_t nr_calls_data(call_frame_t *frame)
{
	return nr_calls(get_data_conf(frame));
}

static inline uint32_t nr_calls_hole(call_frame_t *frame)
{
	return nr_calls(get_hole_conf(frame));
}

static inline void get_one_call_nolock(call_frame_t *frame)
{
	crypt_local_t *local = frame->local;

	++local->nr_calls;

	//gf_log("crypt", GF_LOG_DEBUG, "get %d calls", 1);
}

static inline void get_one_call(call_frame_t *frame)
{
	crypt_local_t *local = frame->local;

	LOCK(&local->call_lock);
	get_one_call_nolock(frame);
	UNLOCK(&local->call_lock);
}

static inline void get_nr_calls_nolock(call_frame_t *frame, int32_t nr)
{
	crypt_local_t *local = frame->local;

	local->nr_calls += nr;

	//gf_log("crypt", GF_LOG_DEBUG, "get %d calls", nr);
}

static inline void get_nr_calls(call_frame_t *frame, int32_t nr)
{
	crypt_local_t *local = frame->local;

	LOCK(&local->call_lock);
	get_nr_calls_nolock(frame, nr);
	UNLOCK(&local->call_lock);
}

static inline int put_one_call(crypt_local_t *local)
{
	uint32_t last = 0;

	LOCK(&local->call_lock);
	if (--local->nr_calls == 0)
		last = 1;

	//gf_log("crypt", GF_LOG_DEBUG, "put %d calls", 1);

	UNLOCK(&local->call_lock);
	return last;
}

static inline int is_appended_write(call_frame_t *frame)
{
	crypt_local_t *local = frame->local;
	struct avec_config *conf = get_data_conf(frame);

	return conf->orig_offset + conf->orig_size > local->old_file_size;
}

static inline int is_ordered_mode(call_frame_t *frame)
{
#if 0
	crypt_local_t *local = frame->local;
	return local->fop == GF_FOP_FTRUNCATE ||
		(local->fop == GF_FOP_WRITE && is_appended_write(frame));
#endif
	return 1;
}

static inline int32_t hole_conv_completed(crypt_local_t *local)
{
	struct avec_config *conf = &local->hole_conf;
	return conf->cursor == conf->acount;
}

static inline int32_t data_write_in_progress(crypt_local_t *local)
{
	return local->active_setup == DATA_ATOM;
}

static inline int32_t parent_is_crypt_xlator(call_frame_t *frame,
					     xlator_t *this)
{
	return frame->parent->this == this;
}

static inline linkop_wind_handler_t linkop_wind_dispatch(glusterfs_fop_t fop)
{
	switch(fop){
	case GF_FOP_LINK:
		return link_wind;
	case GF_FOP_UNLINK:
		return unlink_wind;
	case GF_FOP_RENAME:
		return rename_wind;
	default:
		gf_log("crypt", GF_LOG_ERROR, "Bad link operation %d", fop);
		return NULL;
	}
}

static inline linkop_unwind_handler_t linkop_unwind_dispatch(glusterfs_fop_t fop)
{
	switch(fop){
	case GF_FOP_LINK:
		return link_unwind;
	case GF_FOP_UNLINK:
		return unlink_unwind;
	case GF_FOP_RENAME:
		return rename_unwind;
	default:
		gf_log("crypt", GF_LOG_ERROR, "Bad link operation %d", fop);
		return NULL;
	}
}

static inline mtd_op_t linkop_mtdop_dispatch(glusterfs_fop_t fop)
{
	switch (fop) {
	case GF_FOP_LINK:
		return MTD_APPEND;
	case GF_FOP_UNLINK:
		return MTD_CUT;
	case GF_FOP_RENAME:
		return MTD_OVERWRITE;
	default:
		gf_log("crypt", GF_LOG_WARNING, "Bad link operation %d", fop);
		return MTD_LAST_OP;
	}
}

#endif /* __CRYPT_H__ */

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
