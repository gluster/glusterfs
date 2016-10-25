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

/* Key hierarchy

                   +----------------+
                   | MASTER_VOL_KEY |
                   +-------+--------+
                           |
                           |
          +----------------+----------------+
          |                |                |
          |                |                |
  +-------+------+ +-------+-------+ +------+--------+
  | NMTD_VOL_KEY | | EMTD_FILE_KEY | | DATA_FILE_KEY |
  +-------+------+ +---------------+ +---------------+
          |
          |
  +-------+-------+
  | NMTD_LINK_KEY |
  +---------------+

 */

#if DEBUG_CRYPT
static void check_prf_iters(uint32_t num_iters)
{
	if (num_iters == 0)
		gf_log ("crypt", GF_LOG_DEBUG,
			"bad number of prf iterations : %d", num_iters);
}
#else
#define check_prf_iters(num_iters) noop
#endif /* DEBUG_CRYPT */

unsigned char crypt_fake_oid[16] =
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

/*
 * derive key in the counter mode using
 * sha256-based HMAC as PRF, see
 * NIST Special Publication 800-108, 5.1)
 */

#define PRF_OUTPUT_SIZE  SHA256_DIGEST_LENGTH

static int32_t kderive_init(struct kderive_context *ctx,
			    const unsigned char *pkey, /* parent key */
			    uint32_t pkey_size,  /* parent key size */
			    const unsigned char *idctx, /* id-context */
			    uint32_t idctx_size,
			    crypt_key_type type  /* type of child key */)
{
	unsigned char *pos;
	uint32_t llen = strlen(crypt_keys[type].label);
	/*
	 * Compoud the fixed input data for KDF:
	 * [i]_2 || Label || 0x00 || Id-Context || [L]_2),
	 * NIST SP 800-108, 5.1
	 */
	ctx->fid_len =
		sizeof(uint32_t) +
		llen +
		1 +
		idctx_size +
		sizeof(uint32_t);

	ctx->fid = GF_CALLOC(ctx->fid_len, 1, gf_crypt_mt_key);
	if (!ctx->fid)
		return ENOMEM;
	ctx->out_len = round_up(crypt_keys[type].len >> 3,
				PRF_OUTPUT_SIZE);
	ctx->out = GF_CALLOC(ctx->out_len, 1, gf_crypt_mt_key);
	if (!ctx->out) {
		GF_FREE(ctx->fid);
		return ENOMEM;
	}
	ctx->pkey = pkey;
	ctx->pkey_len = pkey_size;
	ctx->ckey_len = crypt_keys[type].len;

	pos = ctx->fid;

	/* counter will be set up in kderive_rfn() */
	pos += sizeof(uint32_t);

	memcpy(pos, crypt_keys[type].label, llen);
	pos += llen;

	/* set up zero octet */
	*pos = 0;
	pos += 1;

	memcpy(pos, idctx, idctx_size);
	pos += idctx_size;

	*((uint32_t *)pos) = htobe32(ctx->ckey_len);

	return 0;
}

static void kderive_update(struct kderive_context *ctx)
{
	uint32_t i;
#if (OPENSSL_VERSION_NUMBER < 0x1010002f)
	HMAC_CTX hctx;
#endif
        HMAC_CTX *phctx = NULL;
	unsigned char *pos = ctx->out;
	uint32_t *p_iter = (uint32_t *)ctx->fid;
	uint32_t num_iters = ctx->out_len / PRF_OUTPUT_SIZE;

	check_prf_iters(num_iters);

#if (OPENSSL_VERSION_NUMBER < 0x1010002f)
	HMAC_CTX_init(&hctx);
        phctx = &hctx;
#else
        phctx = HMAC_CTX_new();
        /* I guess we presume it was successful? */
#endif
	for (i = 0; i < num_iters; i++) {
		/*
		 * update the iteration number in the fid
		 */
		*p_iter = htobe32(i);
		HMAC_Init_ex(phctx,
			     ctx->pkey, ctx->pkey_len >> 3,
			     EVP_sha256(),
			     NULL);
		HMAC_Update(phctx, ctx->fid, ctx->fid_len);
		HMAC_Final(phctx, pos, NULL);

		pos += PRF_OUTPUT_SIZE;
	}
#if (OPENSSL_VERSION_NUMBER < 0x1010002f)
	HMAC_CTX_cleanup(phctx);
#else
        HMAC_CTX_free(phctx);
#endif
}

static void kderive_final(struct kderive_context *ctx, unsigned char *child)
{
	memcpy(child, ctx->out, ctx->ckey_len >> 3);
	GF_FREE(ctx->fid);
	GF_FREE(ctx->out);
	memset(ctx, 0, sizeof(*ctx));
}

/*
 * derive per-volume key for object ids aithentication
 */
int32_t get_nmtd_vol_key(struct master_cipher_info *master)
{
	int32_t ret;
	struct kderive_context ctx;

	ret = kderive_init(&ctx,
			   master->m_key,
			   master_key_size(),
			   crypt_fake_oid, sizeof(uuid_t), NMTD_VOL_KEY);
	if (ret)
		return ret;
	kderive_update(&ctx);
	kderive_final(&ctx, master->m_nmtd_key);
	return 0;
}

/*
 * derive per-link key for aithentication of non-encrypted
 * meta-data (nmtd)
 */
int32_t get_nmtd_link_key(loc_t *loc,
			  struct master_cipher_info *master,
			  unsigned char *result)
{
	int32_t ret;
	struct kderive_context ctx;

	ret = kderive_init(&ctx,
			   master->m_nmtd_key,
			   nmtd_vol_key_size(),
			   (const unsigned char *)loc->path,
			   strlen(loc->path), NMTD_LINK_KEY);
	if (ret)
		return ret;
	kderive_update(&ctx);
	kderive_final(&ctx, result);
	return 0;
}

/*
 * derive per-file key for encryption and authentication
 * of encrypted part of metadata (emtd)
 */
int32_t get_emtd_file_key(struct crypt_inode_info *info,
			  struct master_cipher_info *master,
			  unsigned char *result)
{
	int32_t ret;
	struct kderive_context ctx;

	ret = kderive_init(&ctx,
			   master->m_key,
			   master_key_size(),
			   info->oid, sizeof(uuid_t), EMTD_FILE_KEY);
	if (ret)
		return ret;
	kderive_update(&ctx);
	kderive_final(&ctx, result);
	return 0;
}

static int32_t data_key_type_by_size(uint32_t keysize, crypt_key_type *type)
{
	int32_t ret = 0;
	switch (keysize) {
	case 256:
		*type = DATA_FILE_KEY_256;
		break;
	case 512:
		*type = DATA_FILE_KEY_512;
		break;
	default:
		gf_log("crypt", GF_LOG_ERROR, "Unsupported data key size %d",
		       keysize);
		ret = ENOTSUP;
		break;
	}
	return ret;
}

/*
 * derive per-file key for data encryption
 */
int32_t get_data_file_key(struct crypt_inode_info *info,
			  struct master_cipher_info *master,
			  uint32_t keysize,
			  unsigned char *key)
{
	int32_t ret;
	struct kderive_context ctx;
	crypt_key_type type;

	ret = data_key_type_by_size(keysize, &type);
	if (ret)
		return ret;
	ret = kderive_init(&ctx,
			   master->m_key,
			   master_key_size(),
			   info->oid, sizeof(uuid_t), type);
	if (ret)
		return ret;
	kderive_update(&ctx);
	kderive_final(&ctx, key);
	return 0;
}

/*
 * NOTE: Don't change existing keys: it will break compatibility;
 */
struct crypt_key crypt_keys[LAST_KEY_TYPE] = {
	[MASTER_VOL_KEY] =
	{ .len = MASTER_VOL_KEY_SIZE << 3,
	  .label = "volume-master",
	},
	[NMTD_VOL_KEY] =
	{ .len = NMTD_VOL_KEY_SIZE << 3,
	  .label = "volume-nmtd-key-generation"
	},
	[NMTD_LINK_KEY] =
	{ .len = 128,
	  .label = "link-nmtd-authentication"
	},
	[EMTD_FILE_KEY] =
	{ .len = 128,
	  .label = "file-emtd-encryption-and-auth"
	},
	[DATA_FILE_KEY_256] =
	{ .len = 256,
	  .label = "file-data-encryption-256"
	},
	[DATA_FILE_KEY_512] =
	{ .len = 512,
	  .label = "file-data-encryption-512"
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
