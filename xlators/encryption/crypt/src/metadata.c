/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "defaults.h"
#include "crypt-common.h"
#include "crypt.h"
#include "metadata.h"

int32_t alloc_format(crypt_local_t *local, size_t size)
{
	if (size > 0) {
		local->format = GF_CALLOC(1, size, gf_crypt_mt_mtd);
		if (!local->format)
			return ENOMEM;
	}
	local->format_size = size;
	return 0;
}

int32_t alloc_format_create(crypt_local_t *local)
{
	return alloc_format(local, new_format_size());
}

void free_format(crypt_local_t *local)
{
	GF_FREE(local->format);
}

/*
 * Check compatibility with extracted metadata
 */
static int32_t check_file_metadata(struct crypt_inode_info *info)
{
	struct object_cipher_info *object = &info->cinfo;

	if (info->nr_minor != CRYPT_XLATOR_ID) {
		gf_log("crypt", GF_LOG_WARNING,
		       "unsupported minor subversion %d", info->nr_minor);
		return EINVAL;
	}
	if (object->o_alg > LAST_CIPHER_ALG) {
		gf_log("crypt", GF_LOG_WARNING,
		       "unsupported cipher algorithm %d",
		       object->o_alg);
		return EINVAL;
	}
	if (object->o_mode > LAST_CIPHER_MODE) {
		gf_log("crypt", GF_LOG_WARNING,
		       "unsupported cipher mode %d",
		       object->o_mode);
		return EINVAL;
	}
	if (object->o_block_bits < CRYPT_MIN_BLOCK_BITS ||
	    object->o_block_bits > CRYPT_MAX_BLOCK_BITS) {
		gf_log("crypt", GF_LOG_WARNING, "unsupported block bits %d",
		       object->o_block_bits);
		return EINVAL;
	}
	/* TBD: check data key size */
	return 0;
}

static size_t format_size_v1(mtd_op_t op, size_t old_size)
{

	switch (op) {
	case MTD_CREATE:
		return sizeof(struct mtd_format_v1);
	case MTD_OVERWRITE:
		return old_size;
	case MTD_APPEND:
		return old_size + NMTD_8_MAC_SIZE;
	case MTD_CUT:
		if (old_size > sizeof(struct mtd_format_v1))
			return old_size - NMTD_8_MAC_SIZE;
		else
			return 0;
	default:
		gf_log("crypt", GF_LOG_WARNING, "Bad mtd operation");
		return 0;
	}
}

/*
 * Calculate size of the updated format string.
 * Returned zero means that we don't need to update the format string.
 */
size_t format_size(mtd_op_t op, size_t old_size)
{
	size_t versioned;

	versioned = mtd_loaders[current_mtd_loader()].format_size(op,
				 old_size - sizeof(struct crypt_format));
	if (versioned != 0)
		return versioned + sizeof(struct crypt_format);
	return 0;
}

/*
 * size of the format string of newly created file (nr_links = 1)
 */
size_t new_format_size(void)
{
	return format_size(MTD_CREATE, 0);
}

/*
 * Calculate per-link MAC by pathname
 */
static int32_t calc_link_mac_v1(struct mtd_format_v1 *fmt,
				loc_t *loc,
				unsigned char *result,
				struct crypt_inode_info *info,
				struct master_cipher_info *master)
{
	int32_t ret;
	unsigned char nmtd_link_key[16];
	CMAC_CTX *cctx;
	size_t len;

	ret = get_nmtd_link_key(loc, master, nmtd_link_key);
	if (ret) {
		gf_log("crypt", GF_LOG_ERROR, "Can not get nmtd link key");
		return -1;
	}
	cctx = CMAC_CTX_new();
	if (!cctx) {
		gf_log("crypt", GF_LOG_ERROR, "CMAC_CTX_new failed");
		return -1;
	}
	ret = CMAC_Init(cctx, nmtd_link_key, sizeof(nmtd_link_key),
			EVP_aes_128_cbc(), 0);
	if (!ret) {
		gf_log("crypt", GF_LOG_ERROR, "CMAC_Init failed");
		CMAC_CTX_free(cctx);
		return -1;
	}
	ret = CMAC_Update(cctx, get_NMTD_V1(info), SIZE_OF_NMTD_V1);
	if (!ret) {
		gf_log("crypt", GF_LOG_ERROR, "CMAC_Update failed");
		CMAC_CTX_free(cctx);
		return -1;
	}
	ret = CMAC_Final(cctx, result, &len);
	CMAC_CTX_free(cctx);
	if (!ret) {
		gf_log("crypt", GF_LOG_ERROR, "CMAC_Final failed");
		return -1;
	}
	return 0;
}

/*
 * Create per-link MAC of index @idx by pathname
 */
static int32_t create_link_mac_v1(struct mtd_format_v1 *fmt,
				  uint32_t idx,
				  loc_t *loc,
				  struct crypt_inode_info *info,
				  struct master_cipher_info *master)
{
	int32_t ret;
	unsigned char *mac;
	unsigned char cmac[16];

	mac = get_NMTD_V1_MAC(fmt) + idx * SIZE_OF_NMTD_V1_MAC;

	ret = calc_link_mac_v1(fmt, loc, cmac, info, master);
	if (ret)
		return -1;
	memcpy(mac, cmac, SIZE_OF_NMTD_V1_MAC);
	return 0;
}

static int32_t create_format_v1(unsigned char *wire,
				loc_t *loc,
				struct crypt_inode_info *info,
				struct master_cipher_info *master)
{
	int32_t ret;
	struct mtd_format_v1 *fmt;
	unsigned char mtd_key[16];
	AES_KEY EMTD_KEY;
	unsigned char nmtd_link_key[16];
	uint32_t ad;
	GCM128_CONTEXT *gctx;

	fmt = (struct mtd_format_v1 *)wire;

	fmt->minor_id = info->nr_minor;
	fmt->alg_id = AES_CIPHER_ALG;
	fmt->dkey_factor = master->m_dkey_size >> KEY_FACTOR_BITS;
	fmt->block_bits = master->m_block_bits;
	fmt->mode_id = master->m_mode;
	/*
	 * retrieve keys for the parts of metadata
	 */
	ret = get_emtd_file_key(info, master, mtd_key);
	if (ret)
		return ret;
	ret = get_nmtd_link_key(loc, master, nmtd_link_key);
	if (ret)
		return ret;

	AES_set_encrypt_key(mtd_key, sizeof(mtd_key)*8, &EMTD_KEY);

	gctx = CRYPTO_gcm128_new(&EMTD_KEY, (block128_f)AES_encrypt);

	/* TBD: Check return values */

	CRYPTO_gcm128_setiv(gctx, info->oid, sizeof(uuid_t));

	ad = htole32(MTD_LOADER_V1);
	ret = CRYPTO_gcm128_aad(gctx, (const unsigned char *)&ad, sizeof(ad));
	if (ret) {
		gf_log("crypt", GF_LOG_ERROR, " CRYPTO_gcm128_aad failed");
		CRYPTO_gcm128_release(gctx);
		return ret;
	}
	ret = CRYPTO_gcm128_encrypt(gctx,
				    get_EMTD_V1(fmt),
				    get_EMTD_V1(fmt),
				    SIZE_OF_EMTD_V1);
	if (ret) {
		gf_log("crypt", GF_LOG_ERROR, " CRYPTO_gcm128_encrypt failed");
		CRYPTO_gcm128_release(gctx);
		return ret;
	}
	/*
	 * set MAC of encrypted part of metadata
	 */
	CRYPTO_gcm128_tag(gctx, get_EMTD_V1_MAC(fmt), SIZE_OF_EMTD_V1_MAC);
	CRYPTO_gcm128_release(gctx);
	/*
	 * set the first MAC of non-encrypted part of metadata
	 */
	return create_link_mac_v1(fmt, 0, loc, info, master);
}

/*
 * Called by fops:
 * ->create();
 * ->link();
 *
 * Pack common and version-specific parts of file's metadata
 * Pre-conditions: @info contains valid object-id.
 */
int32_t create_format(unsigned char *wire,
		      loc_t *loc,
		      struct crypt_inode_info *info,
		      struct master_cipher_info *master)
{
	struct crypt_format *fmt = (struct crypt_format *)wire;

	fmt->loader_id = current_mtd_loader();

	wire += sizeof(struct crypt_format);
	return mtd_loaders[current_mtd_loader()].create_format(wire, loc,
							       info, master);
}

/*
 * Append or overwrite per-link mac of @mac_idx index
 * in accordance with the new pathname
 */
int32_t appov_link_mac_v1(unsigned char *new,
			  unsigned char *old,
			  uint32_t old_size,
			  int32_t mac_idx,
			  loc_t *loc,
			  struct crypt_inode_info *info,
			  struct master_cipher_info *master,
			  crypt_local_t *local)
{
	memcpy(new, old, old_size);
	return create_link_mac_v1((struct mtd_format_v1 *)new, mac_idx,
				  loc, info, master);
}

/*
 * Cut per-link mac of @mac_idx index
 */
static int32_t cut_link_mac_v1(unsigned char *new,
			       unsigned char *old,
			       uint32_t old_size,
			       int32_t mac_idx,
			       loc_t *loc,
			       struct crypt_inode_info *info,
			       struct master_cipher_info *master,
			       crypt_local_t *local)
{
	memcpy(new,
	       old,
	       sizeof(struct mtd_format_v1) + NMTD_8_MAC_SIZE * (mac_idx - 1));

	memcpy(new + sizeof(struct mtd_format_v1) + NMTD_8_MAC_SIZE * (mac_idx - 1),
	       old + sizeof(struct mtd_format_v1) + NMTD_8_MAC_SIZE * mac_idx,
	       old_size - (sizeof(struct mtd_format_v1) + NMTD_8_MAC_SIZE * mac_idx));
	return 0;
}

int32_t update_format_v1(unsigned char *new,
			 unsigned char *old,
				size_t old_len,
				int32_t mac_idx, /* of old name */
				mtd_op_t op,
				loc_t *loc,
				struct crypt_inode_info *info,
				struct master_cipher_info *master,
				crypt_local_t *local)
{
	switch (op) {
	case MTD_APPEND:
		mac_idx = 1 + (old_len - sizeof(struct mtd_format_v1))/8;
	case MTD_OVERWRITE:
		return appov_link_mac_v1(new, old, old_len, mac_idx,
					 loc, info, master, local);
	case MTD_CUT:
		return cut_link_mac_v1(new, old, old_len, mac_idx,
				       loc, info, master, local);
	default:
		gf_log("crypt", GF_LOG_ERROR, "Bad  mtd operation %d", op);
		return -1;
	}
}

/*
 * Called by fops:
 *
 * ->link()
 * ->unlink()
 * ->rename()
 *
 */
int32_t update_format(unsigned char *new,
		      unsigned char *old,
		      size_t old_len,
		      int32_t mac_idx,
		      mtd_op_t op,
		      loc_t *loc,
		      struct crypt_inode_info *info,
		      struct master_cipher_info *master,
		      crypt_local_t *local)
{
	if (!new)
		return 0;
	memcpy(new, old, sizeof(struct crypt_format));

	old += sizeof(struct crypt_format);
	new += sizeof(struct crypt_format);
	old_len -= sizeof(struct crypt_format);

	return mtd_loaders[current_mtd_loader()].update_format(new, old,
							       old_len,
							       mac_idx, op,
							       loc, info,
							       master, local);
}

/*
 * Perform preliminary checks of found metadata
 * Return < 0 on errors;
 * Return number of object-id MACs (>= 1) on success
 */
int32_t check_format_v1(uint32_t len, unsigned char *wire)
{
	uint32_t nr_links;

	if (len < sizeof(struct mtd_format_v1)) {
		gf_log("crypt", GF_LOG_ERROR,
		       "v1-loader: bad metadata size %d", len);
		goto error;
	}
	len -= sizeof(struct mtd_format_v1);
	if (len % sizeof(nmtd_8_mac_t)) {
		gf_log("crypt", GF_LOG_ERROR,
		       "v1-loader: bad metadata format");
		goto error;
	}
	nr_links = 1 + len / sizeof(nmtd_8_mac_t);
	if (nr_links > _POSIX_LINK_MAX)
		goto error;
	return nr_links;
 error:
	return EIO;
}

/*
 * Verify per-link MAC specified by index @idx
 *
 * return:
 * -1 on errors;
 *  0 on failed verification;
 *  1 on successful verification
 */
static int32_t verify_link_mac_v1(struct mtd_format_v1 *fmt,
				  uint32_t idx /* index of the mac to verify */,
				  loc_t *loc,
				  struct crypt_inode_info *info,
				  struct master_cipher_info *master)
{
	int32_t ret;
	unsigned char *mac;
	unsigned char cmac[16];

	mac = get_NMTD_V1_MAC(fmt) + idx * SIZE_OF_NMTD_V1_MAC;

	ret = calc_link_mac_v1(fmt, loc, cmac, info, master);
	if (ret)
		return -1;
	if (memcmp(cmac, mac, SIZE_OF_NMTD_V1_MAC))
		return 0;
	return 1;
}

/*
 * Lookup per-link MAC by pathname.
 *
 * return index of the MAC, if it was found;
 * return < 0 on errors, or if the MAC wasn't found
 */
static int32_t lookup_link_mac_v1(struct mtd_format_v1 *fmt,
				  uint32_t nr_macs,
				  loc_t *loc,
				  struct crypt_inode_info *info,
				  struct master_cipher_info *master)
{
	int32_t ret;
	uint32_t idx;

	for (idx = 0; idx < nr_macs; idx++) {
		ret = verify_link_mac_v1(fmt, idx, loc, info, master);
		if (ret < 0)
			return ret;
		if (ret > 0)
			return idx;
	}
	return -ENOENT;
}

/*
 * Extract version-specific part of metadata
 */
static int32_t open_format_v1(unsigned char *wire,
			      int32_t len,
			      loc_t *loc,
			      struct crypt_inode_info *info,
			      struct master_cipher_info *master,
			      crypt_local_t *local,
			      gf_boolean_t load_info)
{
	int32_t ret;
	int32_t num_nmtd_macs;
	struct mtd_format_v1 *fmt;
	unsigned char mtd_key[16];
	AES_KEY EMTD_KEY;
	GCM128_CONTEXT *gctx;
	uint32_t ad;
	emtd_8_mac_t gmac;
	struct object_cipher_info *object;

	num_nmtd_macs = check_format_v1(len, wire);
	if (num_nmtd_macs <= 0)
		return EIO;

	ret = lookup_link_mac_v1((struct mtd_format_v1 *)wire,
				 num_nmtd_macs, loc, info, master);
	if (ret < 0) {
		gf_log("crypt", GF_LOG_ERROR, "NMTD verification failed");
		return EINVAL;
	}

	local->mac_idx = ret;
	if (load_info == _gf_false)
		/* the case of partial open */
		return 0;

	fmt = GF_CALLOC(1, len, gf_crypt_mt_mtd);
	if (!fmt)
		return ENOMEM;
	memcpy(fmt, wire, len);

	object = &info->cinfo;

	ret = get_emtd_file_key(info, master, mtd_key);
	if (ret) {
		gf_log("crypt", GF_LOG_ERROR, "Can not retrieve metadata key");
		goto out;
	}
	/*
	 * decrypt encrypted meta-data
	 */
	ret = AES_set_encrypt_key(mtd_key, sizeof(mtd_key)*8, &EMTD_KEY);
	if (ret < 0) {
		gf_log("crypt", GF_LOG_ERROR, "Can not set encrypt key");
		ret = EIO;
		goto out;
	}
	gctx = CRYPTO_gcm128_new(&EMTD_KEY, (block128_f)AES_encrypt);
	if (!gctx) {
		gf_log("crypt", GF_LOG_ERROR, "Can not alloc gcm context");
		ret = ENOMEM;
		goto out;
	}
	CRYPTO_gcm128_setiv(gctx, info->oid, sizeof(uuid_t));

	ad = htole32(MTD_LOADER_V1);
	ret = CRYPTO_gcm128_aad(gctx, (const unsigned char *)&ad, sizeof(ad));
	if (ret) {
		gf_log("crypt", GF_LOG_ERROR, " CRYPTO_gcm128_aad failed");
		CRYPTO_gcm128_release(gctx);
		ret = EIO;
		goto out;
	}
	ret = CRYPTO_gcm128_decrypt(gctx,
				    get_EMTD_V1(fmt),
				    get_EMTD_V1(fmt),
				    SIZE_OF_EMTD_V1);
	if (ret) {
		gf_log("crypt", GF_LOG_ERROR, " CRYPTO_gcm128_decrypt failed");
		CRYPTO_gcm128_release(gctx);
		ret = EIO;
		goto out;
	}
	/*
	 * verify metadata
	 */
	CRYPTO_gcm128_tag(gctx, gmac, sizeof(gmac));
	CRYPTO_gcm128_release(gctx);
	if (memcmp(gmac, get_EMTD_V1_MAC(fmt), SIZE_OF_EMTD_V1_MAC)) {
		gf_log("crypt", GF_LOG_ERROR, "EMTD verification failed");
		ret = EINVAL;
		goto out;
	}
	/*
	 * load verified metadata to the private part of inode
	 */
	info->nr_minor = fmt->minor_id;

	object->o_alg = fmt->alg_id;
	object->o_dkey_size = fmt->dkey_factor << KEY_FACTOR_BITS;
	object->o_block_bits = fmt->block_bits;
	object->o_mode = fmt->mode_id;

	ret = check_file_metadata(info);
 out:
	GF_FREE(fmt);
	return ret;
}

/*
 * perform metadata authentication against @loc->path;
 * extract crypt-specific attribtes and populate @info
 * with them (optional)
 */
int32_t open_format(unsigned char *str,
		    int32_t len,
		    loc_t *loc,
		    struct crypt_inode_info *info,
		    struct master_cipher_info *master,
		    crypt_local_t *local,
		    gf_boolean_t load_info)
{
	struct crypt_format *fmt;
	if (len < sizeof(*fmt)) {
		gf_log("crypt", GF_LOG_ERROR, "Bad core format");
		return EIO;
	}
	fmt = (struct crypt_format *)str;

	if (fmt->loader_id >= LAST_MTD_LOADER) {
		gf_log("crypt", GF_LOG_ERROR,
		       "Unsupported loader id %d", fmt->loader_id);
		return EINVAL;
	}
	str += sizeof(*fmt);
	len -= sizeof(*fmt);

	return mtd_loaders[fmt->loader_id].open_format(str,
						       len,
						       loc,
						       info,
						       master,
						       local,
						       load_info);
}

struct crypt_mtd_loader mtd_loaders [LAST_MTD_LOADER] = {
	[MTD_LOADER_V1] =
	{.format_size = format_size_v1,
	 .create_format = create_format_v1,
	 .open_format = open_format_v1,
	 .update_format = update_format_v1
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
