/*
   Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include <sys/uio.h>

#include "xlator.h"
#include "defaults.h"
#include "logging.h"

#include "cdc.h"
#include "cdc-mem-types.h"

static void
cdc_cleanup_iobref (cdc_info_t *ci)
{
        assert(ci->iobref != NULL);
        iobref_clear (ci->iobref);
}

int32_t
cdc_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno,
               struct iovec *vector, int32_t count,
               struct iatt *stbuf, struct iobref *iobref,
               dict_t *xdata)
{
        int         ret   = -1;
        cdc_priv_t *priv  = NULL;
        cdc_info_t  ci    = {0,};

        GF_VALIDATE_OR_GOTO ("cdc", this, default_out);
        GF_VALIDATE_OR_GOTO (this->name, frame, default_out);

        priv = this->private;

        if (op_ret <= 0)
                goto default_out;

        if ( (priv->min_file_size != 0)
             && (op_ret < priv->min_file_size) )
                goto default_out;

        ci.count       = count;
        ci.ibytes      = op_ret;
        ci.vector      = vector;
        ci.buf         = NULL;
        ci.iobref      = NULL;
        ci.ncount      = 0;
        ci.crc         = 0;
        ci.buffer_size = GF_CDC_DEF_BUFFERSIZE;

/* A readv compresses on the server side and decompresses on the client side
 */
        if (priv->op_mode == GF_CDC_MODE_SERVER) {
                ret = cdc_compress (this, priv, &ci, &xdata);
        } else if (priv->op_mode == GF_CDC_MODE_CLIENT) {
                ret = cdc_decompress (this, priv, &ci, xdata);
        } else {
                gf_log (this->name, GF_LOG_ERROR,
                        "Invalid operation mode (%d)", priv->op_mode);
        }

        if (ret)
                goto default_out;

        STACK_UNWIND_STRICT (readv, frame, ci.nbytes, op_errno,
                             ci.vec, ci.ncount, stbuf, iobref,
                             xdata);
        cdc_cleanup_iobref (&ci);
        return 0;

 default_out:
        STACK_UNWIND_STRICT (readv, frame, op_ret, op_errno,
                             vector, count, stbuf, iobref, xdata);
        return 0;
}

int32_t
cdc_readv (call_frame_t *frame, xlator_t *this,
           fd_t *fd, size_t size, off_t offset, uint32_t flags,
           dict_t *xdata)
{
        fop_readv_cbk_t cbk = NULL;

#ifdef HAVE_LIB_Z
        cbk = cdc_readv_cbk;
#else
        cbk = default_readv_cbk;
#endif
        STACK_WIND (frame, cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readv,
                    fd, size, offset, flags, xdata);
        return 0;
}

int32_t
cdc_writev_cbk (call_frame_t *frame,
                void *cookie,
                xlator_t *this,
                int32_t op_ret,
                int32_t op_errno,
                struct iatt *prebuf,
                struct iatt *postbuf, dict_t *xdata)
{

        STACK_UNWIND_STRICT (writev, frame, op_ret, op_errno, prebuf, postbuf, xdata);
        return 0;
}

int32_t
cdc_writev (call_frame_t *frame,
            xlator_t *this,
            fd_t *fd,
            struct iovec *vector,
            int32_t count,
            off_t offset,
            uint32_t flags,
            struct iobref *iobref, dict_t *xdata)
{
        int          ret   = -1;
        cdc_priv_t  *priv  = NULL;
        cdc_info_t   ci    = {0,};
        size_t       isize = 0;

        GF_VALIDATE_OR_GOTO ("cdc", this, default_out);
        GF_VALIDATE_OR_GOTO (this->name, frame, default_out);

        priv = this->private;

        isize =  iov_length(vector, count);

        if (isize <= 0)
            goto default_out;

        if ( (priv->min_file_size != 0) && (isize < priv->min_file_size) )
            goto default_out;

        ci.count       = count;
        ci.ibytes      = isize;
        ci.vector      = vector;
        ci.buf         = NULL;
        ci.iobref      = NULL;
        ci.ncount      = 0;
        ci.crc         = 0;
        ci.buffer_size = GF_CDC_DEF_BUFFERSIZE;

/* A writev compresses on the client side and decompresses on the server side
 */
	    if (priv->op_mode == GF_CDC_MODE_CLIENT) {
		    ret = cdc_compress (this, priv, &ci, &xdata);
	    } else if (priv->op_mode == GF_CDC_MODE_SERVER) {
		    ret = cdc_decompress (this, priv, &ci, xdata);
	    } else {
		    gf_log (this->name, GF_LOG_ERROR, "Invalid operation mode (%d) ", priv->op_mode);
	    }

	    if (ret)
		    goto default_out;

	    STACK_WIND (frame,
                    cdc_writev_cbk,
                    FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->writev,
                    fd, ci.vec, ci.ncount, offset, flags,
                    iobref, xdata);

        cdc_cleanup_iobref (&ci);
        return 0;

 default_out:
        STACK_WIND (frame,
                    cdc_writev_cbk,
                    FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->writev,
                    fd, vector, count, offset, flags,
                    iobref, xdata);
        return 0;
}

int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        if (!this)
                return ret;

        ret = xlator_mem_acct_init (this, gf_cdc_mt_end);

        if (ret != 0) {
                gf_log(this->name, GF_LOG_ERROR, "Memory accounting init"
                       "failed");
                return ret;
        }

        return ret;
}

int32_t
init (xlator_t *this)
{
        int         ret      = -1;
        char       *temp_str = NULL;
        cdc_priv_t *priv     = NULL;

        GF_VALIDATE_OR_GOTO ("cdc", this, err);

        if (!this->children || this->children->next) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Need subvolume == 1");
                goto err;
        }

        if (!this->parents) {
                gf_log (this->name, GF_LOG_WARNING,
                        "Dangling volume. Check volfile");
        }

        priv = GF_CALLOC (1, sizeof (*priv), gf_cdc_mt_priv_t);
        if (!priv) {
                goto err;
        }

        /* Check if debug mode is turned on */
        GF_OPTION_INIT ("debug", priv->debug, bool, err);
        if( priv->debug ) {
                gf_log (this->name, GF_LOG_DEBUG, "CDC debug option turned on");
        }

        /* Set Gzip Window Size */
        GF_OPTION_INIT ("window-size", priv->window_size, int32, err);
        if ( (priv->window_size > GF_CDC_MAX_WINDOWSIZE)
             || (priv->window_size < GF_CDC_DEF_WINDOWSIZE) ) {
                gf_log (this->name, GF_LOG_WARNING,
                        "Invalid gzip window size (%d), using default",
                        priv->window_size);
                priv->window_size = GF_CDC_DEF_WINDOWSIZE;
        }

        /* Set Gzip (De)Compression Level */
        GF_OPTION_INIT ("compression-level", priv->cdc_level, int32, err);
        if ( ((priv->cdc_level < 1) || (priv->cdc_level > 9))
             && (priv->cdc_level != GF_CDC_DEF_COMPRESSION) ) {
                gf_log (this->name, GF_LOG_WARNING,
                        "Invalid gzip (de)compression level (%d),"
                        " using default", priv->cdc_level);
                priv->cdc_level = GF_CDC_DEF_COMPRESSION;
        }

        /* Set Gzip Memory Level */
        GF_OPTION_INIT ("mem-level", priv->mem_level, int32, err);
        if ( (priv->mem_level < 1) || (priv->mem_level > 9) ) {
                gf_log (this->name, GF_LOG_WARNING,
                        "Invalid gzip memory level, using the default");
                priv->mem_level = GF_CDC_DEF_MEMLEVEL;
        }

        /* Set min file size to enable compression */
        GF_OPTION_INIT ("min-size", priv->min_file_size, int32, err);

        /* Mode of operation - Server/Client */
        ret = dict_get_str (this->options, "mode", &temp_str);
        if (ret) {
                gf_log (this->name, GF_LOG_CRITICAL,
                        "Operation mode not specified !!");
                goto err;
        }

        if (GF_CDC_MODE_IS_CLIENT (temp_str)) {
                priv->op_mode = GF_CDC_MODE_CLIENT;
        } else if (GF_CDC_MODE_IS_SERVER (temp_str)) {
                priv->op_mode = GF_CDC_MODE_SERVER;
        } else {
                gf_log (this->name, GF_LOG_CRITICAL,
                        "Bogus operation mode (%s) specified", temp_str);
                goto err;
        }

        this->private = priv;
        gf_log (this->name, GF_LOG_DEBUG, "CDC xlator loaded in (%s) mode",temp_str);
        return 0;

 err:
        if (priv)
                GF_FREE (priv);

        return -1;
}

void
fini (xlator_t *this)
{
        cdc_priv_t *priv = this->private;

        if (priv)
                GF_FREE (priv);
        this->private = NULL;
        return;
}

struct xlator_fops fops = {
        .readv  = cdc_readv,
        .writev = cdc_writev,
};

struct xlator_cbks cbks = {
};

struct volume_options options[] = {
        { .key  = {"window-size"},
          .default_value = "-15",
          .type = GF_OPTION_TYPE_INT,
          .description = "Size of the zlib history buffer."
        },
        { .key  = {"mem-level"},
          .default_value = "8",
          .type = GF_OPTION_TYPE_INT,
          .description = "Memory allocated for internal compression state. "
                         "1 uses minimum memory but is slow and reduces "
                         "compression ratio; memLevel=9 uses maximum memory "
                         "for optimal speed. The default value is 8."
        },
        { .key  = {"compression-level"},
          .default_value = "-1",
          .type = GF_OPTION_TYPE_INT,
          .description = "Compression levels \n"
                         "0 : no compression, 1 : best speed, \n"
                         "9 : best compression, -1 : default compression "
        },
        { .key  = {"min-size"},
          .default_value = "0",
          .type = GF_OPTION_TYPE_INT,
          .description = "Data is compressed only when its size exceeds this."
        },
        { .key  = {"mode"},
          .value = {"server", "client"},
          .type = GF_OPTION_TYPE_STR,
          .description = "Set on the basis of where the xlator is loaded. "
                         "This option should NOT be configured by user."
        },
        { .key = {"debug"},
          .default_value = "false",
          .type = GF_OPTION_TYPE_BOOL,
          .description = "This is used in testing. Will dump compressed data "
                         "to disk as a gzip file."
        },
        { .key  = {NULL}
        },
};
