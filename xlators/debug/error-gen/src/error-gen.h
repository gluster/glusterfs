/*
   Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef _ERROR_GEN_H
#define _ERROR_GEN_H

#include "error-gen-mem-types.h"

#define GF_FAILURE_DEFAULT 10

/*
 * Pseudo-errors refer to errors beyond the scope of traditional <-1, op_errno>
 * returns. This facilitates the ability to return unexpected, but not -1 values
 * and/or to inject operations that lead to implicit error conditions. The range
 * for pseudo errors resides at a high value to avoid conflicts with the errno
 * range.
 */
enum GF_PSEUDO_ERRORS {
	GF_ERROR_SHORT_WRITE = 1000,	/* short writev return value */
	GF_ERROR_MAX
};

typedef struct {
        int enable[GF_FOP_MAXVALUE];
        int op_count;
        int failure_iter_no;
        char *error_no;
        gf_boolean_t random_failure;
        gf_lock_t lock;
} eg_t;

typedef struct {
        int error_no_count;
	int error_no[20];
} sys_error_t;

#endif
