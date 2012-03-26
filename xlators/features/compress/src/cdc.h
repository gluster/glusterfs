/*
   Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef __CDC_H
#define __CDC_H

#ifdef HAVE_LIB_Z
#include "zlib.h"
#endif

#include "xlator.h"

#ifndef MAX_IOVEC
#define MAX_IOVEC 16
#endif

typedef struct cdc_priv {
        int window_size;
        int mem_level;
        int cdc_level;
        int min_file_size;
        int op_mode;
        gf_boolean_t debug;
        gf_lock_t lock;
} cdc_priv_t;

typedef struct cdc_info {
        /* input bits */
        int            count;
        int32_t        ibytes;
        struct iovec  *vector;
        struct iatt   *buf;

        /* output bits */
        int            ncount;
        int            nbytes;
        int            buffer_size;
        struct iovec   vec[MAX_IOVEC];
        struct iobref *iobref;

        /* zlib bits */
#ifdef HAVE_LIB_Z
        z_stream      stream;
#endif
        unsigned long crc;
} cdc_info_t;

#define NVEC(ci) (ci->ncount - 1)
#define CURR_VEC(ci) ci->vec[ci->ncount - 1]
#define THIS_VEC(ci, i) ci->vector[i]

/* Gzip defaults */
#define GF_CDC_DEF_WINDOWSIZE  -15 /* default value */
#define GF_CDC_MAX_WINDOWSIZE  -8  /* max value     */

#ifdef HAVE_LIB_Z
#define GF_CDC_DEF_COMPRESSION Z_DEFAULT_COMPRESSION
#else
#define GF_CDC_DEF_COMPRESSION -1
#endif

#define GF_CDC_DEF_MEMLEVEL    8
#define GF_CDC_DEF_BUFFERSIZE  262144 // 256K - default compression buffer size

/* Operation mode
 * If xlator is loaded on client, readv decompresses and writev compresses
 * If xlator is loaded on server, readv compresses and writev decompresses
 */
#define GF_CDC_MODE_CLIENT   0
#define GF_CDC_MODE_SERVER   1

/* min size of data to do cmpression
 * 0 == compress even 1byte
 */
#define GF_CDC_MIN_CHUNK_SIZE 0

#define GF_CDC_VALIDATION_SIZE 8

#define GF_CDC_OS_ID 0xFF
#define GF_CDC_DEFLATE_CANARY_VAL "deflate"
#define GF_CDC_DEBUG_DUMP_FILE "/tmp/cdcdump.gz"

#define GF_CDC_MODE_IS_CLIENT(m) \
        (strcmp (m, "client") == 0)

#define GF_CDC_MODE_IS_SERVER(m) \
        (strcmp (m, "server") == 0)

int32_t
cdc_compress (xlator_t *this,
              cdc_priv_t *priv,
              cdc_info_t *ci,
              dict_t **xdata);
int32_t
cdc_decompress (xlator_t *this,
                cdc_priv_t *priv,
                cdc_info_t *ci,
                dict_t *xdata);

#endif
