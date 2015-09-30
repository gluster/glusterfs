/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <sys/uio.h>

#include "common-utils.h"
#include "xlator.h"
#include "graph-utils.h"
#include "libglusterfs-messages.h"


struct gf_printer {
        ssize_t (*write) (struct gf_printer *gp, char *buf, size_t len);
        void *priv;
        int  len;
};

static ssize_t
gp_write_file (struct gf_printer *gp, char *buf, size_t len)
{
        FILE *f = gp->priv;

        if (fwrite (buf, len, 1, f) != 1) {
                gf_msg ("graph-print", GF_LOG_ERROR, errno,
                        LG_MSG_FWRITE_FAILED, "fwrite failed");

                return -1;
        }

        return len;
}

static ssize_t
gp_write_buf (struct gf_printer *gp, char *buf, size_t len)
{
        struct iovec *iov = gp->priv;

        if (iov->iov_len < len) {
                gf_msg ("graph-print", GF_LOG_ERROR, 0, LG_MSG_BUFFER_FULL,
                        "buffer full");

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

#define GPPRINTF(gp, fmt, ...) do {                             \
                ret = gpprintf (gp, fmt, ## __VA_ARGS__);       \
                if (ret == -1)                                  \
                        goto out;                               \
                else                                            \
                        gp->len += ret;                             \
        } while (0)

static int
_print_volume_options (dict_t *d, char *k, data_t *v,
                           void *tmp)
{
        struct gf_printer *gp  = tmp;
        int                ret = 0;
        GPPRINTF (gp, "    option %s %s\n", k, v->data);
        return 0;
out:
        /* means, it is a failure */
        return -1;
}

static int
glusterfs_graph_print (struct gf_printer *gp, glusterfs_graph_t *graph)
{
        xlator_t      *trav = NULL;
        xlator_list_t *xch = NULL;
        int            ret = 0;
        ssize_t        len = 0;

        if (!graph->first)
                return 0;

        for (trav = graph->first; trav->next; trav = trav->next);
        for (; trav; trav = trav->prev) {
                GPPRINTF (gp, "volume %s\n    type %s\n", trav->name,
                          trav->type);

                ret = dict_foreach (trav->options, _print_volume_options, gp);
                if (ret)
                        goto out;

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
        len = gp->len;
        if (ret == -1) {
                gf_msg ("graph-print", GF_LOG_ERROR, 0, LG_MSG_PRINT_FAILED,
                        "printing failed");

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
                gf_msg ("graph-print", GF_LOG_ERROR, errno,
                        LG_MSG_DIR_OP_FAILED, "cannot open /dev/null");

                return NULL;
        }
        len = glusterfs_graph_print_file (f, graph);
        fclose (f);
        if (len == -1)
                return NULL;

        buf = GF_CALLOC (1, len + 1, gf_common_mt_graph_buf);
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
