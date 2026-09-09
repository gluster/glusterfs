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
    EVP_MD_CTX *mdctx;
    // Use the MD5 digest algorithm
    const EVP_MD *md = EVP_md5();

    mdctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(mdctx, md, NULL);
    EVP_DigestUpdate(mdctx, data, len);
    EVP_DigestFinal_ex(mdctx, md5, NULL);
    EVP_MD_CTX_free(mdctx);
}
