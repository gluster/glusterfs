/*
  Copyright (c) 2010 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU Affero General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Affero General Public License for more details.

  You should have received a copy of the GNU Affero General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include <sys/uio.h>

#include "common-utils.h"
#include "xlator.h"
#include "graph-mem-types.h"
#include "graph-utils.h"



struct gf_printer {
        ssize_t (*write) (struct gf_printer *gp, char *buf, size_t len);
        void *priv;
};

static ssize_t
gp_write_file (struct gf_printer *gp, char *buf, size_t len)
{
        FILE *f = gp->priv;

        if (fwrite (buf, len, 1, f) != 1) {
                gf_log ("graph-print", GF_LOG_ERROR, "fwrite failed (%s)",
                        strerror (errno));

                return -1;
        }

        return len;
}

static ssize_t
gp_write_buf (struct gf_printer *gp, char *buf, size_t len)
{
        struct iovec *iov = gp->priv;

        if (iov->iov_len < len) {
                gf_log ("graph-print", GF_LOG_ERROR, "buffer full");

                return -1;
        }

        memcpy (iov->iov_base, buf, len);
        iov->iov_base += len;
        iov->iov_len  -= len;

        return len;
}

static int
gpprintf (struct gf_printer *gp, const char *format, ...)
{
        va_list arg;
        char *str = NULL;
        int ret = 0;

        va_start (arg, format);
        ret = gf_vasprintf (&str, format, arg);
        va_end (arg);

        if (ret < 0)
                return ret;

        ret = gp->write (gp, str, ret);

        GF_FREE (str);

        return ret;
}

static int
glusterfs_graph_print (struct gf_printer *gp, glusterfs_graph_t *graph)
{
#define GPPRINTF(gp, fmt, ...) do {                             \
                ret = gpprintf (gp, fmt, ## __VA_ARGS__);       \
                if (ret == -1)                                  \
                        goto out;                               \
                else                                            \
                        len += ret;                             \
        } while (0)

        xlator_t      *trav = NULL;
        data_pair_t   *pair = NULL;
        xlator_list_t *xch = NULL;
        int            ret = 0;
        ssize_t        len = 0;

        if (!graph->first)
                return 0;

        for (trav = graph->first; trav->next; trav = trav->next);
        for (; trav; trav = trav->prev) {
                GPPRINTF (gp, "volume %s\n    type %s\n", trav->name,
                          trav->type);

                for (pair =  trav->options->members_list; pair && pair->next;
                     pair = pair->next);
                for (; pair; pair = pair->prev)
                        GPPRINTF (gp, "    option %s %s\n", pair->key,
                                  pair->value->data);

                if (trav->children) {
                        GPPRINTF (gp, "    subvolumes");

                        for (xch = trav->children; xch; xch = xch->next)
                                GPPRINTF (gp, " %s", xch->xlator->name);

                        GPPRINTF (gp, "\n");
                }

                GPPRINTF (gp, "end-volume\n");
                if (trav != graph->first)
                        GPPRINTF (gp, "\n");
        }

out:
        if (ret == -1) {
                gf_log ("graph-print", GF_LOG_ERROR, "printing failed");

                return -1;
        }

        return len;

#undef GPPRINTF
}

int
glusterfs_graph_print_file (FILE *file, glusterfs_graph_t *graph)
{
        struct gf_printer gp = { .write = gp_write_file,
                                 .priv  = file
        };

        return glusterfs_graph_print (&gp, graph);
}

char *
glusterfs_graph_print_buf (glusterfs_graph_t *graph)
{
        FILE *f = NULL;
        struct iovec iov = {0,};
        int len = 0;
        char *buf = NULL;
        struct gf_printer gp = { .write = gp_write_buf,
                                 .priv  = &iov
        };

        f = fopen ("/dev/null", "a");
        if (!f) {
                gf_log ("graph-print", GF_LOG_ERROR,
                        "cannot open /dev/null (%s)", strerror (errno));

                return NULL;
        }
        len = glusterfs_graph_print_file (f, graph);
        fclose (f);
        if (len == -1)
                return NULL;

        buf = GF_CALLOC (1, len + 1, gf_graph_mt_buf);
        if (!buf) {
                return NULL;
        }
        iov.iov_base = buf;
        iov.iov_len  = len;

        len = glusterfs_graph_print (&gp, graph);
        if (len == -1) {
                GF_FREE (buf);

                return NULL;
        }

        return buf;
}
