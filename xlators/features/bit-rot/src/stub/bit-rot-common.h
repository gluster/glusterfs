 /*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef __BIT_ROT_COMMON_H__
#define __BIT_ROT_COMMON_H__

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"

/**
 * on-disk formats for ongoing version and object signature.
 */
typedef struct br_version {
        unsigned long ongoingversion;
        uint32_t timebuf[2];
} br_version_t;

typedef struct br_signature {
        int8_t signaturetype;

        unsigned long signedversion;
        unsigned long currentversion;

        char signature[0];
} br_signature_t;

/**
 * in-memory representation of signature used by signer for object
 * signing.
 */
typedef struct br_isignature_in {
        int8_t signaturetype;            /* signature type            */

        unsigned long signedversion;     /* version against which the
                                            object was signed         */

        char signature[0];               /* object signature          */
} br_isignature_t;

/**
 * in-memory representation of signature used by scrubber for object
 * verification.
 */
typedef struct br_isignature_out {
        char stale;                      /* stale signature?          */

        uint32_t time[2];                /* time when the object
                                            got dirtied               */

        int8_t signaturetype;            /* hash type                 */
        char signature[0];               /* signature (hash)          */
} br_isignature_out_t;

typedef struct br_stub_init {
        uint32_t timebuf[2];
        char export[PATH_MAX];
} br_stub_init_t;

typedef enum {
        BR_SIGNATURE_TYPE_VOID   = -1,   /* object is not signed       */
        BR_SIGNATURE_TYPE_ZERO   = 0,    /* min boundary               */
        BR_SIGNATURE_TYPE_SHA256 = 1,    /* signed with SHA256         */
        BR_SIGNATURE_TYPE_MAX    = 2,    /* max boundary               */
} br_signature_type;

/* BitRot stub start time (virtual xattr) */
#define GLUSTERFS_GET_BR_STUB_INIT_TIME  "trusted.glusterfs.bit-rot.stub-init"

static inline int
br_is_signature_type_valid (int8_t signaturetype)
{
        return ( (signaturetype > BR_SIGNATURE_TYPE_ZERO)
                 && (signaturetype < BR_SIGNATURE_TYPE_MAX) );
}

static inline void
br_set_default_ongoingversion (br_version_t *buf, uint32_t *tv)
{
        buf->ongoingversion = BITROT_DEFAULT_CURRENT_VERSION;
        buf->timebuf[0] = tv[0];
        buf->timebuf[1] = tv[1];
}

static inline void
br_set_default_signature (br_signature_t *buf, size_t *size)
{
        buf->signaturetype = (int8_t) BR_SIGNATURE_TYPE_VOID;
        buf->signedversion = BITROT_DEFAULT_SIGNING_VERSION;
        buf->currentversion = BITROT_DEFAULT_CURRENT_VERSION;

        *size = sizeof (br_signature_t); /* no signature */
}

static inline void
br_set_ongoingversion (br_version_t *buf,
                       unsigned long version, uint32_t *tv)
{
        buf->ongoingversion = version;
        buf->timebuf[0] = tv[0];
        buf->timebuf[1] = tv[1];
}

static inline void
br_set_signature (br_signature_t *buf,
                  br_isignature_t *sign,
                  unsigned long currentversion,
                  size_t signaturelen, size_t *size)
{
        buf->signaturetype  = sign->signaturetype;
        buf->signedversion  = ntohl (sign->signedversion);
        buf->currentversion = currentversion;

        memcpy (buf->signature, sign->signature, signaturelen);
        *size = sizeof (br_signature_t) + signaturelen;
}

#endif /* __BIT_ROT_COMMON_H__ */
