/*
   Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef _CHANGELOG_ENCODERS_H
#define _CHANGELOG_ENCODERS_H

#include "xlator.h"
#include "defaults.h"

#include "changelog-helpers.h"

#define CHANGELOG_STORE_ASCII(priv, buf, off, gfid, gfid_len, cld) do { \
                CHANGELOG_FILL_BUFFER (buffer, off,                     \
                                       priv->maps[cld->cld_type], 1);   \
                CHANGELOG_FILL_BUFFER (buffer,                          \
                                       off, gfid, gfid_len);            \
        } while (0)

#define CHANGELOG_STORE_BINARY(priv, buf, off, gfid, cld) do {          \
                CHANGELOG_FILL_BUFFER (buffer, off,                     \
                                       priv->maps[cld->cld_type], 1);   \
                CHANGELOG_FILL_BUFFER (buffer,                          \
                                       off, gfid, sizeof (uuid_t));     \
        } while (0)

size_t
entry_fn (void *data, char *buffer, gf_boolean_t encode);
size_t
del_entry_fn (void *data, char *buffer, gf_boolean_t encode);
size_t
fop_fn (void *data, char *buffer, gf_boolean_t encode);
size_t
number_fn (void *data, char *buffer, gf_boolean_t encode);
void
entry_free_fn (void *data);
void
del_entry_free_fn (void *data);
int
changelog_encode_binary (xlator_t *, changelog_log_data_t *);
int
changelog_encode_ascii (xlator_t *, changelog_log_data_t *);
void
changelog_encode_change(changelog_priv_t *);

#endif /* _CHANGELOG_ENCODERS_H */
