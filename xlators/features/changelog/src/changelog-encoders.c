/*
   Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include "changelog-encoders.h"

size_t
entry_fn (void *data, char *buffer, gf_boolean_t encode)
{
        char    *tmpbuf = NULL;
        size_t  bufsz  = 0;
        struct changelog_entry_fields *ce = NULL;

        ce = (struct changelog_entry_fields *) data;

        if (encode) {
                tmpbuf = uuid_utoa (ce->cef_uuid);
                CHANGELOG_FILL_BUFFER (buffer, bufsz, tmpbuf, strlen (tmpbuf));
        } else {
                CHANGELOG_FILL_BUFFER (buffer, bufsz,
                                       ce->cef_uuid, sizeof (uuid_t));
        }

        CHANGELOG_FILL_BUFFER (buffer, bufsz, "/", 1);
        CHANGELOG_FILL_BUFFER (buffer, bufsz,
                               ce->cef_bname, strlen (ce->cef_bname));
        return bufsz;
}

size_t
del_entry_fn (void *data, char *buffer, gf_boolean_t encode)
{
        char    *tmpbuf = NULL;
        size_t  bufsz  = 0;
        struct changelog_entry_fields *ce = NULL;

        ce = (struct changelog_entry_fields *) data;

        if (encode) {
                tmpbuf = uuid_utoa (ce->cef_uuid);
                CHANGELOG_FILL_BUFFER (buffer, bufsz, tmpbuf, strlen (tmpbuf));
        } else {
                CHANGELOG_FILL_BUFFER (buffer, bufsz,
                                       ce->cef_uuid, sizeof (uuid_t));
        }

        CHANGELOG_FILL_BUFFER (buffer, bufsz, "/", 1);
        CHANGELOG_FILL_BUFFER (buffer, bufsz,
                               ce->cef_bname, strlen (ce->cef_bname));
        CHANGELOG_FILL_BUFFER (buffer, bufsz, "\0", 1);

        if (ce->cef_path[0] == '\0') {
                CHANGELOG_FILL_BUFFER (buffer, bufsz, "\0", 1);
        } else {
                CHANGELOG_FILL_BUFFER (buffer, bufsz,
                                       ce->cef_path, strlen (ce->cef_path));
        }

        return bufsz;
}

size_t
fop_fn (void *data, char *buffer, gf_boolean_t encode)
{
        char buf[10]          = {0,};
        size_t         bufsz = 0;
        glusterfs_fop_t fop   = 0;

        fop = *(glusterfs_fop_t *) data;

        if (encode) {
                (void) snprintf (buf, sizeof (buf), "%d", fop);
                CHANGELOG_FILL_BUFFER (buffer, bufsz, buf, strlen (buf));
        } else
                CHANGELOG_FILL_BUFFER (buffer, bufsz, &fop, sizeof (fop));

        return bufsz;
}

size_t
number_fn (void *data, char *buffer, gf_boolean_t encode)
{
        size_t       bufsz = 0;
        unsigned int nr    = 0;
        char buf[20]       = {0,};

        nr = *(unsigned int *) data;

        if (encode) {
                (void) snprintf (buf, sizeof (buf), "%u", nr);
                CHANGELOG_FILL_BUFFER (buffer, bufsz, buf, strlen (buf));
        } else
                CHANGELOG_FILL_BUFFER (buffer, bufsz, &nr, sizeof (unsigned int));

        return bufsz;
}

void
entry_free_fn (void *data)
{
        changelog_opt_t *co = data;

        if (!co)
                return;

        GF_FREE (co->co_entry.cef_bname);
}

void
del_entry_free_fn (void *data)
{
        changelog_opt_t *co = data;

        if (!co)
                return;

        GF_FREE (co->co_entry.cef_bname);
        GF_FREE (co->co_entry.cef_path);
}

/**
 * try to write all data in one shot
 */

static void
changelog_encode_write_xtra (changelog_log_data_t *cld,
                             char *buffer, size_t *off, gf_boolean_t encode)
{
        int              i      = 0;
        size_t           offset = 0;
        void            *data   = NULL;
        changelog_opt_t *co     = NULL;

        offset = *off;

        co = (changelog_opt_t *) cld->cld_ptr;

        for (; i < cld->cld_xtra_records; i++, co++) {
                CHANGELOG_FILL_BUFFER (buffer, offset, "\0", 1);

                switch (co->co_type) {
                case CHANGELOG_OPT_REC_FOP:
                        data = &co->co_fop;
                        break;
                case CHANGELOG_OPT_REC_ENTRY:
                        data = &co->co_entry;
                        break;
                case CHANGELOG_OPT_REC_UINT32:
                        data = &co->co_uint32;
                        break;
                }

                if (co->co_convert)
                        offset += co->co_convert (data,
                                                  buffer + offset, encode);
                else /* no coversion: write it out as it is */
                        CHANGELOG_FILL_BUFFER (buffer, offset,
                                               data, co->co_len);
        }

        *off = offset;
}

int
changelog_encode_ascii (xlator_t *this, changelog_log_data_t *cld)
{
        size_t            off      = 0;
        size_t            gfid_len = 0;
        char             *gfid_str = NULL;
        char             *buffer   = NULL;
        changelog_priv_t *priv     = NULL;

        priv = this->private;

        gfid_str = uuid_utoa (cld->cld_gfid);
        gfid_len = strlen (gfid_str);

        /* extra bytes for decorations */
        buffer = alloca (gfid_len + cld->cld_ptr_len + 10);
        CHANGELOG_STORE_ASCII (priv, buffer,
                               off, gfid_str, gfid_len, cld);

        if (cld->cld_xtra_records)
                changelog_encode_write_xtra (cld, buffer, &off, _gf_true);

        CHANGELOG_FILL_BUFFER (buffer, off, "\0", 1);

        return changelog_write_change (priv, buffer, off);
}

int
changelog_encode_binary (xlator_t *this, changelog_log_data_t *cld)
{
        size_t            off    = 0;
        char             *buffer = NULL;
        changelog_priv_t *priv   = NULL;

        priv = this->private;

        /* extra bytes for decorations */
        buffer = alloca (sizeof (uuid_t) + cld->cld_ptr_len + 10);
        CHANGELOG_STORE_BINARY (priv, buffer, off, cld->cld_gfid, cld);

        if (cld->cld_xtra_records)
                changelog_encode_write_xtra (cld, buffer, &off, _gf_false);

        CHANGELOG_FILL_BUFFER (buffer, off, "\0", 1);

        return changelog_write_change (priv, buffer, off);
}

static struct changelog_encoder
cb_encoder[] = {
        [CHANGELOG_ENCODE_BINARY] =
        {
                .encoder = CHANGELOG_ENCODE_BINARY,
                .encode = changelog_encode_binary,
        },
        [CHANGELOG_ENCODE_ASCII] =
        {
                .encoder = CHANGELOG_ENCODE_ASCII,
                .encode = changelog_encode_ascii,
        },
};

void
changelog_encode_change(changelog_priv_t *priv)
{
        priv->ce = &cb_encoder[priv->encode_mode];
}
