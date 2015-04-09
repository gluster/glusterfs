/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef __BIT_ROT_OBJECT_VERSION_H
#define __BIT_ROT_OBJECT_VERSION_H

/**
 * on-disk formats for ongoing version and object signature.
 */
typedef struct br_version {
        unsigned long ongoingversion;
        uint32_t timebuf[2];
} br_version_t;

typedef struct __attribute__ ((__packed__)) br_signature {
        int8_t signaturetype;

        unsigned long signedversion;

        char signature[0];
} br_signature_t;

#endif
