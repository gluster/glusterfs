/*
  Copyright (c) 2008-2013 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __CRYPT_COMMON_H__
#define __CRYPT_COMMON_H__

#define INVAL_SUBVERSION_NUMBER (0xff)
#define CRYPT_INVAL_OP   (GF_FOP_NULL)

#define CRYPTO_FORMAT_PREFIX  "trusted.glusterfs.crypt.att.cfmt"
#define FSIZE_XATTR_PREFIX    "trusted.glusterfs.crypt.att.size"
#define SUBREQ_PREFIX         "trusted.glusterfs.crypt.msg.sreq"
#define FSIZE_MSG_PREFIX      "trusted.glusterfs.crypt.msg.size"
#define DE_MSG_PREFIX         "trusted.glusterfs.crypt.msg.dent"
#define REQUEST_ID_PREFIX     "trusted.glusterfs.crypt.msg.rqid"
#define MSGFLAGS_PREFIX       "trusted.glusterfs.crypt.msg.xfgs"


/* messages for crypt_open() */
#define MSGFLAGS_REQUEST_MTD_RLOCK   1 /* take read lock and don't unlock */
#define MSGFLAGS_REQUEST_MTD_WLOCK   2 /* take write lock and don't unlock */

#define AES_BLOCK_BITS (4) /* AES_BLOCK_SIZE == 1 << AES_BLOCK_BITS */

#define noop   do {; } while (0)
#define cassert(cond) ({ switch (-1) { case (cond): case 0: break; } })
#define __round_mask(x, y) ((__typeof__(x))((y)-1))
#define round_up(x, y) ((((x)-1) | __round_mask(x, y))+1)

/*
 * Format of file's metadata
 */
struct crypt_format {
	uint8_t loader_id;  /* version of metadata loader */
	uint8_t versioned[0]; /* file's metadata of specific version */
} __attribute__((packed));

typedef enum {
	AES_CIPHER_ALG,
	LAST_CIPHER_ALG
} cipher_alg_t;

typedef enum {
	XTS_CIPHER_MODE,
	LAST_CIPHER_MODE
} cipher_mode_t;

typedef enum {
	MTD_LOADER_V1,
	LAST_MTD_LOADER
} mtd_loader_id;

static inline void msgflags_set_mtd_rlock(uint32_t *flags)
{
	*flags |= MSGFLAGS_REQUEST_MTD_RLOCK;
}

static inline void msgflags_set_mtd_wlock(uint32_t *flags)
{
	*flags |= MSGFLAGS_REQUEST_MTD_WLOCK;
}

static inline gf_boolean_t msgflags_check_mtd_rlock(uint32_t *flags)
{
	return *flags & MSGFLAGS_REQUEST_MTD_RLOCK;
}

static inline gf_boolean_t msgflags_check_mtd_wlock(uint32_t *flags)
{
	return *flags & MSGFLAGS_REQUEST_MTD_WLOCK;
}

static inline gf_boolean_t msgflags_check_mtd_lock(uint32_t *flags)
{
	return  msgflags_check_mtd_rlock(flags) ||
		msgflags_check_mtd_wlock(flags);
}

/*
 * returns number of logical blocks occupied
 * (maybe partially) by @count bytes
 * at offset @start.
 */
static inline off_t logical_blocks_occupied(uint64_t start, off_t count,
					    int blkbits)
{
	return ((start + count - 1) >> blkbits) - (start >> blkbits) + 1;
}

/*
 * are two bytes (represented by offsets @off1
 * and @off2 respectively) in the same logical
 * block.
 */
static inline int in_same_lblock(uint64_t off1, uint64_t off2,
				 int blkbits)
{
	return off1 >> blkbits == off2 >> blkbits;
}

static inline void dump_cblock(xlator_t *this, unsigned char *buf)
{
	gf_log(this->name, GF_LOG_DEBUG,
	       "dump cblock: %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x",
	       (buf)[0],
	       (buf)[1],
	       (buf)[2],
	       (buf)[3],
	       (buf)[4],
	       (buf)[5],
	       (buf)[6],
	       (buf)[7],
	       (buf)[8],
	       (buf)[9],
	       (buf)[10],
	       (buf)[11],
	       (buf)[12],
	       (buf)[13],
	       (buf)[14],
	       (buf)[15]);
}

#endif /* __CRYPT_COMMON_H__ */

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
