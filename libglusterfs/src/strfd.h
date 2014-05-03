/*
   Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef _STRFD_H
#define _STRFD_H

typedef struct {
        void *data;
        size_t alloc_size;
        size_t size;
        off_t pos;
} strfd_t;

strfd_t *strfd_open();

int strprintf(strfd_t *strfd, const char *fmt, ...)
        __attribute__ ((__format__ (__printf__, 2, 3)));

int strvprintf(strfd_t *strfd, const char *fmt, va_list ap);

int strfd_close(strfd_t *strfd);

#endif
