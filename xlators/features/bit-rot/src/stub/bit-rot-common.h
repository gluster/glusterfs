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

#include "glusterfs.h"
#include "bit-rot-object-version.h"

#define BR_VXATTR_VERSION   (1 << 0)
#define BR_VXATTR_SIGNATURE (1 << 1)

#define BR_VXATTR_SIGN_MISSING (BR_VXATTR_SIGNATURE)
#define BR_VXATTR_ALL_MISSING                           \
        (BR_VXATTR_VERSION | BR_VXATTR_SIGNATURE)

#define BR_BAD_OBJ_CONTAINER (uuid_t){0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8}

typedef enum br_vxattr_state {
        BR_VXATTR_STATUS_FULL     = 0,
        BR_VXATTR_STATUS_MISSING  = 1,
        BR_VXATTR_STATUS_UNSIGNED = 2,
        BR_VXATTR_STATUS_INVALID  = 3,
} br_vxattr_status_t;

typedef enum br_sign_state {
        BR_SIGN_INVALID     = -1,
        BR_SIGN_NORMAL      = 0,
        BR_SIGN_REOPEN_WAIT = 1,
        BR_SIGN_QUICK       = 2,
} br_sign_state_t;

static inline br_vxattr_status_t
br_version_xattr_state (dict_t *xattr, br_version_t **obuf,
                        br_signature_t **sbuf, gf_boolean_t *objbad)
{
        int32_t             ret    = 0;
        int32_t             vxattr = 0;
        br_vxattr_status_t  status;
        void               *data   = NULL;

        /**
         * The key being present in the dict indicates the xattr was set on
         * disk. The presence of xattr itself as of now is suffecient to say
         * the the object is bad.
         */
        *objbad = _gf_false;
        ret = dict_get_bin (xattr, BITROT_OBJECT_BAD_KEY, (void **)&data);
        if (!ret)
                *objbad = _gf_true;

        ret = dict_get_bin (xattr, BITROT_CURRENT_VERSION_KEY, (void **)obuf);
        if (ret)
                vxattr |= BR_VXATTR_VERSION;

        ret = dict_get_bin (xattr, BITROT_SIGNING_VERSION_KEY, (void **)sbuf);
        if (ret)
                vxattr |= BR_VXATTR_SIGNATURE;

        switch (vxattr) {
        case 0:
                status = BR_VXATTR_STATUS_FULL;
                break;
        case BR_VXATTR_SIGN_MISSING:
                status = BR_VXATTR_STATUS_UNSIGNED;
                break;
        case BR_VXATTR_ALL_MISSING:
                status = BR_VXATTR_STATUS_MISSING;
                break;
        default:
                status = BR_VXATTR_STATUS_INVALID;
        }

        return status;
}

/**
 * in-memory representation of signature used by signer for object
 * signing.
 */
typedef struct br_isignature_in {
        int8_t signaturetype;            /* signature type            */

        unsigned long signedversion;     /* version against which the
                                            object was signed         */

        size_t signaturelen;             /* signature length          */
        char signature[0];               /* object signature          */
} br_isignature_t;

/**
 * in-memory representation of signature used by scrubber for object
 * verification.
 */
typedef struct br_isignature_out {
        char stale;                      /* stale signature?          */

        unsigned long version;           /* current signed version    */

        uint32_t time[2];                /* time when the object
                                            got dirtied               */

        int8_t signaturetype;            /* hash type                 */
        size_t signaturelen;             /* signature length          */
        char   signature[0];             /* signature (hash)          */
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

/* signing/reopen hint */
#define BR_OBJECT_RESIGN 0
#define BR_OBJECT_REOPEN  1
#define BR_REOPEN_SIGN_HINT_KEY  "trusted.glusterfs.bit-rot.reopen-hint"

static inline int
br_is_signature_type_valid (int8_t signaturetype)
{
        return ((signaturetype > BR_SIGNATURE_TYPE_ZERO)
                 && (signaturetype < BR_SIGNATURE_TYPE_MAX));
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
                  br_isignature_t *sign, size_t signaturelen, size_t *size)
{
        buf->signaturetype  = sign->signaturetype;
        buf->signedversion  = ntohl (sign->signedversion);

        memcpy (buf->signature, sign->signature, signaturelen);
        *size = sizeof (br_signature_t) + signaturelen;
}

#endif /* __BIT_ROT_COMMON_H__ */
