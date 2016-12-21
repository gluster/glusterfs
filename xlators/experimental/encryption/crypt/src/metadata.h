/*
  Copyright (c) 2008-2013 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __METADATA_H__
#define __METADATA_H__

#define NMTD_8_MAC_SIZE   (8)
#define EMTD_8_MAC_SIZE   (8)

typedef uint8_t nmtd_8_mac_t[NMTD_8_MAC_SIZE];
typedef uint8_t emtd_8_mac_t[EMTD_8_MAC_SIZE] ;

/*
 * Version "v1" of file's metadata.
 * Metadata of this version has 4 components:
 *
 * 1) EMTD (Encrypted part of MeTaData);
 * 2) NMTD (Non-encrypted part of MeTaData);
 * 3) EMTD_MAC; (EMTD Message Authentication Code);
 * 4) Array of per-link NMTD MACs (for every (hard)link it includes
 *    exactly one MAC)
 */
struct mtd_format_v1 {
	/* EMTD, encrypted part of meta-data */
	uint8_t            alg_id;  /* cipher algorithm id (only AES for now) */
        uint8_t            mode_id; /* cipher mode id; (only XTS for now) */
	uint8_t            block_bits; /* encoded block size */
	uint8_t            minor_id; /* client translator id */
	uint8_t            dkey_factor; /* encoded size of the data key */
	/* MACs */
	emtd_8_mac_t       gmac; /* MAC of the encrypted meta-data, 8 bytes */
	nmtd_8_mac_t       omac; /* per-link MACs of the non-encrypted
				  * meta-data: at least one such MAC is always
				  * present */
} __attribute__((packed));

/*
 * NMTD, the non-encrypted part of metadata of version "v1"
 * is file's gfid, which is generated on trusted machines.
 */
#define SIZE_OF_NMTD_V1 (sizeof(uuid_t))
#define SIZE_OF_EMTD_V1 (offsetof(struct mtd_format_v1, gmac) -		\
			 offsetof(struct mtd_format_v1, alg_id))
#define SIZE_OF_NMTD_V1_MAC (NMTD_8_MAC_SIZE)
#define SIZE_OF_EMTD_V1_MAC (EMTD_8_MAC_SIZE)

static inline unsigned char *get_EMTD_V1(struct mtd_format_v1 *format)
{
        return &format->alg_id;
}

static inline unsigned char *get_NMTD_V1(struct crypt_inode_info *info)
{
        return info->oid;
}

static inline unsigned char *get_EMTD_V1_MAC(struct mtd_format_v1 *format)
{
        return format->gmac;
}

static inline unsigned char *get_NMTD_V1_MAC(struct mtd_format_v1 *format)
{
        return format->omac;
}

#endif /* __METADATA_H__ */
