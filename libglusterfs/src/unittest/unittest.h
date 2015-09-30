/*
  Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _GF_UNITTEST_H_
#define _GF_UNITTEST_H_

#ifdef UNIT_TESTING
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka_pbc.h>
#include <cmocka.h>

extern void mock_assert(const int result, const char* const expression,
                        const char * const file, const int line);

// Change GF_CALLOC and GF_FREE to use
// cmocka memory allocation versions
#ifdef UNIT_TESTING
#undef GF_CALLOC
#define GF_CALLOC(n, s, t) test_calloc(n, s)
#undef GF_FREE
#define GF_FREE test_free

/* Catch intended assert()'s while unit-testing */
extern void mock_assert(const int result, const char* const expression,
                        const char * const file, const int line);

#undef assert
#define assert(expression) \
            mock_assert((int)(expression), #expression, __FILE__, __LINE__);
#endif
#else
#define REQUIRE(p) /**/
#define ENSURE(p) /**/
#endif

#endif /* _GF_UNITTEST */
