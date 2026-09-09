/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <openssl/evp.h>
#include <openssl/sha.h>
#include <zlib.h>
#include <stdint.h>
#include <string.h>

/*
 * The "weak" checksum required for the rsync algorithm.
 *
 * Note: these functions are only called to compute checksums on
 * pathnames; they don't need to handle arbitrarily long strings of
 * data. Thus int32_t and uint32_t are sufficient
 */
uint32_t
gf_rsync_weak_checksum(unsigned char *buf, size_t len)
{
    return adler32(0, buf, len);
}

/*
 * The "strong" checksum required for the rsync algorithm.
 */
void
gf_rsync_strong_checksum(unsigned char *data, size_t len,
                         unsigned char *sha256_md)
{
    SHA256((const unsigned char *)data, len, sha256_md);
}

void
gf_rsync_md5_checksum(unsigned char *data, size_t len, unsigned char *md5)
{
    EVP_MD_CTX *ctx = NULL;

    ctx = EVP_MD_CTX_new();
    if (!ctx)
        return;

    if (EVP_DigestInit_ex(ctx, EVP_md5(), NULL) != 1)
        goto out;

    if (EVP_DigestUpdate(ctx, data, len) != 1)
        goto out;

    EVP_DigestFinal_ex(ctx, md5, NULL);

out:
    EVP_MD_CTX_free(ctx);
}
