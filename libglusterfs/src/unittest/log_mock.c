/*
  Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "logging.h"
#include "xlator.h"

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <inttypes.h>

#include <cmocka.h>

int _gf_log (const char *domain, const char *file,
             const char *function, int32_t line, gf_loglevel_t level,
             const char *fmt, ...)
{
    return 0;
}

int _gf_log_callingfn (const char *domain, const char *file,
                       const char *function, int32_t line, gf_loglevel_t level,
                       const char *fmt, ...)
{
    return 0;
}

int _gf_log_nomem (const char *domain, const char *file,
                   const char *function, int line, gf_loglevel_t level,
                   size_t size)
{
    return 0;
}

int _gf_msg_nomem (const char *domain, const char *file,
                   const char *function, int line, gf_loglevel_t level,
                   size_t size)
{
	return 0;
}

void
gf_log_globals_init (void *data, gf_loglevel_t level) {}
