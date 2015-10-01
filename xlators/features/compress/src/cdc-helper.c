/*
   Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include "glusterfs.h"
#include "logging.h"
#include "syscall.h"

#include "cdc.h"
#include "cdc-mem-types.h"

#ifdef HAVE_LIB_Z
#include "zlib.h"
#endif

#ifdef HAVE_LIB_Z
/* gzip header looks something like this
 * (RFC 1950)
 *
 * +---+---+---+---+---+---+---+---+---+---+
 * |ID1|ID2|CM |FLG|     MTIME     |XFL|OS |
 * +---+---+---+---+---+---+---+---+---+---+
 *
 * Data is usually sent without this header i.e
 * Data sent = <compressed-data> + trailer(8)
 * The trailer contains the checksum.
 *
 * gzip_header is added only during debugging.
 * Refer to the function cdc_dump_iovec_to_disk
 */
static const char gzip_header[10] =
        {
                '\037', '\213', Z_DEFLATED, 0,
                0,      0,      0,          0,
                0,      GF_CDC_OS_ID
        };

static int32_t
cdc_next_iovec (xlator_t *this, cdc_info_t *ci)
{
        int ret = -1;

        ci->ncount++;
        /* check for iovec overflow -- should not happen */
        if (ci->ncount == MAX_IOVEC) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Zlib output buffer overflow"
                        " ->ncount (%d) | ->MAX_IOVEC (%d)",
                        ci->ncount, MAX_IOVEC);
                goto out;
        }

        ret = 0;

 out:
        return ret;
}

static void
cdc_put_long (unsigned char *string, unsigned long x)
{
        string[0] = (unsigned char) (x & 0xff);
        string[1] = (unsigned char) ((x & 0xff00) >> 8);
        string[2] = (unsigned char) ((x & 0xff0000) >> 16);
        string[3] = (unsigned char) ((x & 0xff000000) >> 24);
}

static unsigned long
cdc_get_long (unsigned char *buf)
{
        return ((unsigned long) buf[0])
                | (((unsigned long) buf[1]) << 8)
                | (((unsigned long) buf[2]) << 16)
                | (((unsigned long) buf[3]) << 24);
}

static int32_t
cdc_init_gzip_trailer (xlator_t *this, cdc_priv_t *priv, cdc_info_t *ci)
{
        int   ret = -1;
        char *buf = NULL;

        ret = cdc_next_iovec (this, ci);
        if (ret)
                goto out;

        buf = CURR_VEC(ci).iov_base =
                (char *) GF_CALLOC (1, GF_CDC_VALIDATION_SIZE,
                                    gf_cdc_mt_gzip_trailer_t);

        if (!CURR_VEC(ci).iov_base)
                goto out;

        CURR_VEC(ci).iov_len = GF_CDC_VALIDATION_SIZE;

        cdc_put_long ((unsigned char *)&buf[0], ci->crc);
        cdc_put_long ((unsigned char *)&buf[4], ci->stream.total_in);

        ret = 0;

 out:
        return ret;
}

static int32_t
cdc_alloc_iobuf_and_init_vec (xlator_t *this,
                              cdc_priv_t *priv, cdc_info_t *ci,
                              int size)
{
        int           ret       = -1;
        int           alloc_len = 0;
        struct iobuf *iobuf     = NULL;

        ret = cdc_next_iovec (this, ci);
        if (ret)
                goto out;

        alloc_len = size ? size : ci->buffer_size;

        iobuf = iobuf_get2 (this->ctx->iobuf_pool, alloc_len);
        if (!iobuf)
                goto out;

        ret = iobref_add (ci->iobref, iobuf);
        if (ret)
                goto out;

        /* Initialize this iovec */
        CURR_VEC(ci).iov_base = iobuf->ptr;
        CURR_VEC(ci).iov_len  = alloc_len;

        ret = 0;

 out:
        return ret;
}

static void
cdc_init_zlib_output_stream (cdc_priv_t *priv, cdc_info_t *ci, int size)
{
        ci->stream.next_out  = (unsigned char *) CURR_VEC(ci).iov_base;
        ci->stream.avail_out = size ? size : ci->buffer_size;
}

/* This routine is for testing and debugging only.
 * Data written = header(10) + <compressed-data> + trailer(8)
 * So each gzip dump file is at least 18 bytes in size.
 */
void
cdc_dump_iovec_to_disk (xlator_t *this, cdc_info_t *ci, const char *file)
{
        int    i      = 0;
        int    fd     = 0;
        size_t written = 0;
        size_t total_written = 0;

        fd = open (file, O_WRONLY|O_CREAT|O_TRUNC, 0777 );
        if (fd < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Cannot open file: %s", file);
                return;
        }

        written = sys_write (fd, (char *) gzip_header, 10);
        total_written += written;
        for (i = 0; i < ci->ncount; i++) {
                written = sys_write (fd, (char *) ci->vec[i].iov_base, ci->vec[i].iov_len);
                total_written += written;
        }

        gf_log (this->name, GF_LOG_DEBUG,
                        "dump'd %zu bytes to %s", total_written, GF_CDC_DEBUG_DUMP_FILE );

        sys_close (fd);
}

static int32_t
cdc_flush_libz_buffer (cdc_priv_t *priv, xlator_t *this, cdc_info_t *ci,
                       int (*libz_func)(z_streamp, int),
                       int flush)
{
        int32_t ret = Z_OK;
        int done = 0;
        unsigned int deflate_len = 0;

        for (;;) {
                deflate_len = ci->buffer_size - ci->stream.avail_out;

                if (deflate_len != 0) {
                        CURR_VEC(ci).iov_len = deflate_len;

                        ret = cdc_alloc_iobuf_and_init_vec (this, priv, ci, 0);
                        if (ret) {
                                ret = Z_MEM_ERROR;
                                break;
                        }

                        /* Re-position Zlib output buffer */
                        cdc_init_zlib_output_stream (priv, ci, 0);
                }

                if (done) {
                        ci->ncount--;
                        break;
                }

                ret = libz_func (&ci->stream, flush);

                if (ret == Z_BUF_ERROR) {
                        ret = Z_OK;
                        ci->ncount--;
                        break;
                }

                done = (ci->stream.avail_out != 0 || ret == Z_STREAM_END);

                if (ret != Z_OK && ret != Z_STREAM_END)
                        break;
        }

        return ret;
}

static int32_t
do_cdc_compress (struct iovec *vec, xlator_t *this, cdc_priv_t *priv,
                 cdc_info_t *ci)
{
        int ret = -1;

        /* Initialize defalte */
        ret = deflateInit2 (&ci->stream, priv->cdc_level, Z_DEFLATED,
                            priv->window_size, priv->mem_level,
                            Z_DEFAULT_STRATEGY);

        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "unable to init Zlib (retval: %d)", ret);
                goto out;
        }

        ret = cdc_alloc_iobuf_and_init_vec (this, priv, ci, 0);
        if (ret)
                goto out;

        /* setup output buffer */
        cdc_init_zlib_output_stream (priv, ci, 0);

        /* setup input buffer */
        ci->stream.next_in  = (unsigned char *) vec->iov_base;
        ci->stream.avail_in = vec->iov_len;

        ci->crc = crc32 (ci->crc, (const Bytef *) vec->iov_base, vec->iov_len);

        gf_log (this->name, GF_LOG_DEBUG, "crc=%lu len=%d buffer_size=%d",
                ci->crc, ci->stream.avail_in, ci->buffer_size);

        /* compress !! */
        while (ci->stream.avail_in != 0) {
                if (ci->stream.avail_out == 0) {

                        CURR_VEC(ci).iov_len = ci->buffer_size;

                        ret = cdc_alloc_iobuf_and_init_vec (this, priv, ci, 0);
                        if (ret)
                                break;

                        /* Re-position Zlib output buffer */
                        cdc_init_zlib_output_stream (priv, ci, 0);
                }

                ret = deflate (&ci->stream, Z_NO_FLUSH);
                if (ret != Z_OK)
                        break;
        }

 out:
        return ret;
}

int32_t
cdc_compress (xlator_t *this, cdc_priv_t *priv, cdc_info_t *ci,
              dict_t **xdata)
{
        int ret = -1;
        int i   = 0;

        ci->iobref = iobref_new ();
        if (!ci->iobref)
                goto out;

        if (!*xdata) {
                *xdata = dict_new ();
                if (!*xdata) {
                        gf_log (this->name, GF_LOG_ERROR, "Cannot allocate xdata"
                                " dict");
                        goto out;
                }
        }

        /* data */
        for (i = 0; i < ci->count; i++) {
                ret = do_cdc_compress (&ci->vector[i], this, priv, ci);
                if (ret != Z_OK)
                        goto deflate_cleanup_out;
        }

        /* flush zlib buffer */
        ret = cdc_flush_libz_buffer (priv, this, ci, deflate, Z_FINISH);
        if (!(ret == Z_OK || ret == Z_STREAM_END)) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Compression Error: ret (%d)", ret);
                ret = -1;
                goto deflate_cleanup_out;
        }

        /* trailer */
        ret = cdc_init_gzip_trailer (this, priv, ci);
        if (ret)
                goto deflate_cleanup_out;

        gf_log (this->name, GF_LOG_DEBUG,
                "Compressed %ld to %ld bytes",
                ci->stream.total_in, ci->stream.total_out);

        ci->nbytes = ci->stream.total_out + GF_CDC_VALIDATION_SIZE;

        /* set deflated canary value for identification */
        ret = dict_set_int32 (*xdata, GF_CDC_DEFLATE_CANARY_VAL, 1);
        if (ret) {
                /* Send uncompressed data if we can't _tell_ the client
                 * that deflated data is on it's way. So, we just log
                 * the faliure and continue as usual.
                 */
                 gf_log (this->name, GF_LOG_ERROR,
                 "Data deflated, but could not set canary"
                 " value in dict for identification");
        }

        /* This is to be used in testing */
        if ( priv->debug ) {
                cdc_dump_iovec_to_disk (this, ci, GF_CDC_DEBUG_DUMP_FILE );
        }

 deflate_cleanup_out:
        (void) deflateEnd(&ci->stream);

 out:
        return ret;
}


/* deflate content is checked by the presence of a canary
 * value in the dict as the key
 */
static int32_t
cdc_check_content_for_deflate (dict_t *xdata)
{
        return dict_get (xdata, GF_CDC_DEFLATE_CANARY_VAL) ? -1 : 0;
}

static unsigned long
cdc_extract_crc (char *trailer)
{
        return cdc_get_long ((unsigned char *) &trailer[0]);
}

static unsigned long
cdc_extract_size (char *trailer)
{
        return cdc_get_long ((unsigned char *) &trailer[4]);
}

static int32_t
cdc_validate_inflate (cdc_info_t *ci, unsigned long crc,
                      unsigned long len)
{
        return !((crc == ci->crc)
                 /* inflated length is hidden inside
                  * Zlib stream struct */
                 && (len == ci->stream.total_out));
}

static int32_t
do_cdc_decompress (xlator_t *this, cdc_priv_t *priv, cdc_info_t *ci)
{
        int            ret          = -1;
        int            i            = 0;
        int            len          = 0;
        char          *inflte       = NULL;
        char          *trailer      = NULL;
        struct iovec   vec          = {0,};
        unsigned long  computed_crc = 0;
        unsigned long  computed_len = 0;

        ret = inflateInit2 (&ci->stream, priv->window_size);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Zlib: Unable to initialize inflate");
                goto out;
        }

        vec = THIS_VEC(ci, 0);

        trailer = (char *) (((char *) vec.iov_base) + vec.iov_len
                - GF_CDC_VALIDATION_SIZE);

        /* CRC of uncompressed data */
        computed_crc = cdc_extract_crc (trailer);

        /* size of uncomrpessed data */
        computed_len = cdc_extract_size (trailer);

        gf_log (this->name, GF_LOG_DEBUG, "crc=%lu len=%lu buffer_size=%d",
                computed_crc, computed_len, ci->buffer_size);

        inflte = vec.iov_base ;
        len = vec.iov_len - GF_CDC_VALIDATION_SIZE;

        /* allocate buffer of the original length of the data */
        ret = cdc_alloc_iobuf_and_init_vec (this, priv, ci, 0);
        if (ret)
                goto out;

        /* setup output buffer */
        cdc_init_zlib_output_stream (priv, ci, 0);

        /* setup input buffer */
        ci->stream.next_in = (unsigned char *) inflte;
        ci->stream.avail_in = len;

        while (ci->stream.avail_in != 0) {
                if (ci->stream.avail_out == 0) {
                        CURR_VEC(ci).iov_len = ci->buffer_size;

                        ret = cdc_alloc_iobuf_and_init_vec (this, priv, ci, 0);
                        if (ret)
                                break;

                        /* Re-position Zlib output buffer */
                        cdc_init_zlib_output_stream (priv, ci, 0);
                }

                ret = inflate (&ci->stream, Z_NO_FLUSH);
                if (ret == Z_STREAM_ERROR)
                        break;
        }

        /* flush zlib buffer */
        ret = cdc_flush_libz_buffer (priv, this, ci, inflate, Z_SYNC_FLUSH);
        if (!(ret == Z_OK || ret == Z_STREAM_END)) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Decompression Error: ret (%d)", ret);
                ret = -1;
                goto out;
        }

        /* compute CRC of the uncompresses data to check for
         * correctness */

        for (i = 0; i < ci->ncount; i++) {
                ci->crc = crc32 (ci->crc,
                                 (const Bytef *) ci->vec[i].iov_base,
                                 ci->vec[i].iov_len);
        }

        /* validate inflated data */
        ret = cdc_validate_inflate (ci, computed_crc, computed_len);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Checksum or length mismatched in inflated data");
        }

 out:
        return ret;
}

int32_t
cdc_decompress (xlator_t *this, cdc_priv_t *priv, cdc_info_t *ci,
                dict_t *xdata)
{
        int32_t ret = -1;

        /* check for deflate content */
        if (!cdc_check_content_for_deflate (xdata)) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Content not deflated, passing through ...");
                goto passthrough_out;
        }

        ci->iobref = iobref_new ();
        if (!ci->iobref)
                goto passthrough_out;

        /* do we need to do this? can we assume that one iovec
         * will hold per request data every time?
         *
         * server/client protocol seems to deal with a single
         * iovec even if op_ret > 1M. So, it looks ok to
         * assume that a single iovec will contain all the
         * data (This saves us a lot from finding the trailer
         * and the data since it could have been split-up onto
         * two adjacent iovec's.
         *
         * But, in case this translator is loaded above quick-read
         * for some reason, then it's entirely possible that we get
         * multiple iovec's...
         *
         * This case (handled below) is not tested. (by loading the
         * xlator below quick-read)
         */

        /* @@ I_HOPE_THIS_IS_NEVER_HIT */
        if (ci->count > 1) {
                gf_log (this->name, GF_LOG_WARNING, "unable to handle"
                        " multiple iovecs (%d in number)", ci->count);
                goto inflate_cleanup_out;
                /* TODO: coallate all iovecs in one */
        }

        ret = do_cdc_decompress (this, priv, ci);
        if (ret)
                goto inflate_cleanup_out;

        ci->nbytes = ci->stream.total_out;

        gf_log (this->name, GF_LOG_DEBUG,
                "Inflated %ld to %ld bytes",
                ci->stream.total_in, ci->stream.total_out);

 inflate_cleanup_out:
        (void) inflateEnd (&ci->stream);

 passthrough_out:
        return ret;
}

#endif
