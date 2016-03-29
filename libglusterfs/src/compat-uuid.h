/*
 * Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
 * This file is part of GlusterFS.
 *
 * This file is licensed to you under your choice of the GNU Lesser
 * General Public License, version 3 or any later version (LGPLv3 or
 * later), or the GNU General Public License, version 2 (GPLv2), in all
 * cases as published by the Free Software Foundation.
 */

#ifndef _GF_UUID_H
#define _GF_UUID_H

#if defined(HAVE_LIBUUID) /* Linux like libuuid.so */

#include <uuid.h>

static inline void
gf_uuid_clear (uuid_t uuid)
{
        uuid_clear (uuid);
}

static inline int
gf_uuid_compare (uuid_t u1, uuid_t u2)
{
        return uuid_compare (u1, u2);
}

static inline void
gf_uuid_copy (uuid_t dst, const uuid_t src)
{
        uuid_copy (dst, src);
}

static inline void
gf_uuid_generate (uuid_t uuid)
{
        uuid_generate  (uuid);
}

static inline int
gf_uuid_is_null (uuid_t uuid)
{
        return uuid_is_null (uuid);
}

static inline int
gf_uuid_parse (const char *in, uuid_t uuid)
{
        return uuid_parse (in, uuid);
}

static inline void
gf_uuid_unparse (const uuid_t uuid, char *out)
{
        uuid_unparse (uuid, out);
}

/* TODO: add more uuid APIs, use constructs like this:
#elif defined(__NetBSD__) * NetBSD libc *

#include <string.h>

static inline void
gf_uuid_clear (uuid_t uuid)
{
        memset (uuid, 0, sizeof (uuid_t));
}

*/

#else /* use bundled Linux like libuuid from contrib/uuid/ */

#include "uuid.h"

#endif /* HAVE_UUID */
#endif /* _GF_UUID_H */
