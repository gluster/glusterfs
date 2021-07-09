/*
   Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include <stdarg.h>

#include "glusterfs/mem-types.h"
#include "glusterfs/mem-pool.h"
#include "glusterfs/strfd.h"
#include "glusterfs/common-utils.h"

#ifdef HAVE_OPEN_MEMSTREAM

int
strfd_open(strfd_t *strfd)
{
    memset(strfd, 0, sizeof(strfd_t));
    strfd->fp = open_memstream(&strfd->data, &strfd->size);
    return strfd->fp ? 0 : -1;
}

int
strvprintf(strfd_t *strfd, const char *fmt, va_list ap)
{
    int size = vfprintf(strfd->fp, fmt, ap);
    /* According to the manual, flushing is required to update 'data'
       and 'size' fields which are directly accessed by strfd users. */
    fflush(strfd->fp);
    return size;
}

void
strfd_close(strfd_t *strfd)
{
    fclose(strfd->fp);
    /* Not GF_FREE because was not GF_ALLOC'ed. */
    free(strfd->data);
}

#else /* not HAVE_OPEN_MEMSTREAM */

int
strfd_open(strfd_t *strfd)
{
    memset(strfd, 0, sizeof(strfd_t));
    return 0;
}

int
strvprintf(strfd_t *strfd, const char *fmt, va_list ap)
{
    char *str = NULL;
    int size = 0;

    size = vasprintf(&str, fmt, ap);

    if (size < 0)
        return size;

    if (!strfd->alloc_size) {
        strfd->data = GF_CALLOC(max(size + 1, 4096), 1,
                                gf_common_mt_strfd_data_t);
        if (!strfd->data) {
            free(str); /* NOT GF_FREE */
            return -1;
        }
        strfd->alloc_size = max(size + 1, 4096);
    }

    if (strfd->alloc_size <= (strfd->size + size)) {
        char *tmp_ptr = NULL;
        int new_size = max(
            (strfd->alloc_size * 2),
            gf_roundup_next_power_of_two(strfd->size + size + 1));
        tmp_ptr = GF_REALLOC(strfd->data, new_size);
        if (!tmp_ptr) {
            free(str); /* NOT GF_FREE */
            return -1;
        }
        strfd->alloc_size = new_size;
        strfd->data = tmp_ptr;
    }

    /* Copy the trailing '\0', but do not account for it in ->size.
       This allows safe use of strfd->data as a string. */
    memcpy(strfd->data + strfd->size, str, size + 1);
    strfd->size += size;

    free(str); /* NOT GF_FREE */

    return size;
}

void
strfd_close(strfd_t *strfd)
{
    GF_FREE(strfd->data);
}

#endif /* HAVE_OPEN_MEMSTREAM */

int
strprintf(strfd_t *strfd, const char *fmt, ...)
{
    int ret = 0;
    va_list ap;

    va_start(ap, fmt);
    ret = strvprintf(strfd, fmt, ap);
    va_end(ap);

    return ret;
}
