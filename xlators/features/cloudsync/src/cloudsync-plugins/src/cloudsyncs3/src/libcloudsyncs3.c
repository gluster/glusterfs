/*
  Copyright (c) 2018 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <stdlib.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/crypto.h>
#include <curl/curl.h>
#include <glusterfs/xlator.h>
#include <glusterfs/glusterfs.h>
#include "libcloudsyncs3.h"
#include "cloudsync-common.h"

#define RESOURCE_SIZE 4096

store_methods_t store_ops = {
    .fop_download = aws_download_s3,
    .fop_init = aws_init,
    .fop_reconfigure = aws_reconfigure,
    .fop_fini = aws_fini,
};

typedef struct aws_private {
    char *hostname;
    char *bucketid;
    char *awssekey;
    char *awskeyid;
    gf_boolean_t abortdl;
    pthread_spinlock_t lock;
} aws_private_t;

void *
aws_init(xlator_t *this)
{
    aws_private_t *priv = NULL;
    char *temp_str = NULL;
    int ret = 0;

    priv = GF_CALLOC(1, sizeof(aws_private_t), gf_libaws_mt_aws_private_t);
    if (!priv) {
        gf_msg(this->name, GF_LOG_ERROR, 0, 0, "insufficient memory");
        return NULL;
    }

    priv->abortdl = _gf_false;

    pthread_spin_init(&priv->lock, PTHREAD_PROCESS_PRIVATE);

    pthread_spin_lock(&(priv->lock));
    {
        if (dict_get_str(this->options, "s3plugin-seckey", &temp_str) == 0) {
            priv->awssekey = gf_strdup(temp_str);
            if (!priv->awssekey) {
                gf_msg(this->name, GF_LOG_ERROR, ENOMEM, 0,
                       "initializing aws secret key failed");
                ret = -1;
                goto unlock;
            }
        }

        if (dict_get_str(this->options, "s3plugin-keyid", &temp_str) == 0) {
            priv->awskeyid = gf_strdup(temp_str);
            if (!priv->awskeyid) {
                gf_msg(this->name, GF_LOG_ERROR, ENOMEM, 0,
                       "initializing aws key ID failed");
                ret = -1;
                goto unlock;
            }
        }

        if (dict_get_str(this->options, "s3plugin-bucketid", &temp_str) == 0) {
            priv->bucketid = gf_strdup(temp_str);
            if (!priv->bucketid) {
                gf_msg(this->name, GF_LOG_ERROR, ENOMEM, 0,
                       "initializing aws bucketid failed");

                ret = -1;
                goto unlock;
            }
        }

        if (dict_get_str(this->options, "s3plugin-hostname", &temp_str) == 0) {
            priv->hostname = gf_strdup(temp_str);
            if (!priv->hostname) {
                gf_msg(this->name, GF_LOG_ERROR, ENOMEM, 0,
                       "initializing aws hostname failed");

                ret = -1;
                goto unlock;
            }
        }

        gf_msg_debug(this->name, 0,
                     "stored key: %s id: %s "
                     "bucketid %s hostname: %s",
                     priv->awssekey, priv->awskeyid, priv->bucketid,
                     priv->hostname);
    }
unlock:
    pthread_spin_unlock(&(priv->lock));

    if (ret == -1) {
        GF_FREE(priv->awskeyid);
        GF_FREE(priv->awssekey);
        GF_FREE(priv->bucketid);
        GF_FREE(priv->hostname);
        GF_FREE(priv);
        priv = NULL;
    }

    return (void *)priv;
}

int
aws_reconfigure(xlator_t *this, dict_t *options)
{
    aws_private_t *priv = NULL;
    char *temp_str = NULL;
    int ret = 0;
    cs_private_t *cspriv = NULL;

    cspriv = this->private;

    priv = cspriv->stores->config;

    if (!priv) {
        gf_msg(this->name, GF_LOG_ERROR, 0, 0, "null priv");
        return -1;
    }

    pthread_spin_lock(&(priv->lock));
    {
        if (dict_get_str(options, "s3plugin-seckey", &temp_str) == 0) {
            priv->awssekey = gf_strdup(temp_str);
            if (!priv->awssekey) {
                gf_msg(this->name, GF_LOG_ERROR, ENOMEM, 0,
                       "initializing aws secret key failed");
                ret = -1;
                goto out;
            }
        }

        if (dict_get_str(options, "s3plugin-keyid", &temp_str) == 0) {
            priv->awskeyid = gf_strdup(temp_str);
            if (!priv->awskeyid) {
                gf_msg(this->name, GF_LOG_ERROR, ENOMEM, 0,
                       "initializing aws key ID failed");
                ret = -1;
                goto out;
            }
        }

        if (dict_get_str(options, "s3plugin-bucketid", &temp_str) == 0) {
            priv->bucketid = gf_strdup(temp_str);
            if (!priv->bucketid) {
                gf_msg(this->name, GF_LOG_ERROR, ENOMEM, 0,
                       "initializing aws bucketid failed");
                ret = -1;
                goto out;
            }
        }

        if (dict_get_str(options, "s3plugin-hostname", &temp_str) == 0) {
            priv->hostname = gf_strdup(temp_str);
            if (!priv->hostname) {
                gf_msg(this->name, GF_LOG_ERROR, ENOMEM, 0,
                       "initializing aws hostname failed");
                ret = -1;
                goto out;
            }
        }
    }
out:
    pthread_spin_unlock(&(priv->lock));

    gf_msg_debug(this->name, 0,
                 "stored key: %s id: %s "
                 "bucketid %s hostname: %s",
                 priv->awssekey, priv->awskeyid, priv->bucketid,
                 priv->hostname);

    return ret;
}

void
aws_fini(void *config)
{
    aws_private_t *priv = NULL;

    priv = (aws_private_t *)priv;

    if (priv) {
        GF_FREE(priv->hostname);
        GF_FREE(priv->bucketid);
        GF_FREE(priv->awssekey);
        GF_FREE(priv->awskeyid);

        pthread_spin_destroy(&priv->lock);
        GF_FREE(priv);
    }
}

int32_t
mem_acct_init(xlator_t *this)
{
    int ret = -1;

    GF_VALIDATE_OR_GOTO("dht", this, out);

    ret = xlator_mem_acct_init(this, gf_libaws_mt_end + 1);

    if (ret != 0) {
        gf_msg(this->name, GF_LOG_ERROR, 0, 0, "Memory accounting init failed");
        return ret;
    }
out:
    return ret;
}
char *
aws_form_request(char *resource, char **date, char *reqtype, char *bucketid,
                 char *filepath)
{
    char httpdate[256];
    time_t ctime;
    struct tm *gtime = NULL;
    char *sign_req = NULL;
    int signreq_len = -1;
    int date_len = -1;
    int res_len = -1;

    ctime = gf_time();
    gtime = gmtime(&ctime);

    date_len = strftime(httpdate, sizeof(httpdate),
                        "%a, %d %b %Y %H:%M:%S +0000", gtime);

    *date = gf_strndup(httpdate, date_len);
    if (*date == NULL) {
        gf_msg("CS", GF_LOG_ERROR, ENOMEM, 0,
               "memory allocation "
               "failure for date");
        goto out;
    }

    res_len = snprintf(resource, RESOURCE_SIZE, "%s/%s", bucketid, filepath);

    gf_msg_debug("CS", 0, "resource %s", resource);

    /* 6 accounts for the 4 new line chars, one forward slash and
     * one null char */
    signreq_len = res_len + date_len + strlen(reqtype) + 6;

    sign_req = GF_MALLOC(signreq_len, gf_common_mt_char);
    if (sign_req == NULL) {
        gf_msg("CS", GF_LOG_ERROR, ENOMEM, 0,
               "memory allocation "
               "failure for sign_req");
        goto out;
    }

    snprintf(sign_req, signreq_len, "%s\n\n%s\n%s\n/%s", reqtype, "", *date,
             resource);

out:
    return sign_req;
}

char *
aws_b64_encode(const unsigned char *input, int length)
{
    BIO *bio, *b64;
    BUF_MEM *bptr;
    char *buff = NULL;

    b64 = BIO_new(BIO_f_base64());
    bio = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64, bio);
    BIO_write(b64, input, length);
    BIO_flush(b64);
    BIO_get_mem_ptr(b64, &bptr);

    buff = GF_MALLOC(bptr->length, gf_common_mt_char);
    memcpy(buff, bptr->data, bptr->length - 1);
    buff[bptr->length - 1] = 0;

    BIO_free_all(b64);

    return buff;
}

char *
aws_sign_request(char *const str, char *awssekey)
{
#if (OPENSSL_VERSION_NUMBER < 0x1010002f)
    HMAC_CTX ctx;
#endif
    HMAC_CTX *pctx = NULL;
    ;

    unsigned char md[256];
    unsigned len;
    char *base64 = NULL;

#if (OPENSSL_VERSION_NUMBER < 0x1010002f)
    HMAC_CTX_init(&ctx);
    pctx = &ctx;
#else
    pctx = HMAC_CTX_new();
#endif
    HMAC_Init_ex(pctx, awssekey, strlen(awssekey), EVP_sha1(), NULL);
    HMAC_Update(pctx, (unsigned char *)str, strlen(str));
    HMAC_Final(pctx, (unsigned char *)md, &len);

#if (OPENSSL_VERSION_NUMBER < 0x1010002f)
    HMAC_CTX_cleanup(pctx);
#else
    HMAC_CTX_free(pctx);
#endif
    base64 = aws_b64_encode(md, len);

    return base64;
}

int
aws_dlwritev_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int op_ret,
                 int op_errno, struct iatt *prebuf, struct iatt *postbuf,
                 dict_t *xdata)
{
    aws_private_t *priv = NULL;

    if (op_ret == -1) {
        gf_msg(this->name, GF_LOG_ERROR, 0, op_errno,
               "write failed "
               ". Aborting Download");

        priv = this->private;
        pthread_spin_lock(&(priv->lock));
        {
            priv->abortdl = _gf_true;
        }
        pthread_spin_unlock(&(priv->lock));
    }

    CS_STACK_DESTROY(frame);

    return op_ret;
}

size_t
aws_write_callback(void *dlbuf, size_t size, size_t nitems, void *mainframe)
{
    call_frame_t *frame = NULL;
    fd_t *dlfd = NULL;
    int ret = 0;
    cs_local_t *local = NULL;
    struct iovec iov = {
        0,
    };
    struct iobref *iobref = NULL;
    struct iobuf *iobuf = NULL;
    struct iovec dliov = {
        0,
    };
    size_t tsize = 0;
    xlator_t *this = NULL;
    cs_private_t *xl_priv = NULL;
    aws_private_t *priv = NULL;
    call_frame_t *dlframe = NULL;

    frame = (call_frame_t *)mainframe;
    this = frame->this;
    xl_priv = this->private;
    priv = xl_priv->stores->config;

    pthread_spin_lock(&(priv->lock));
    {
        /* returning size other than the size passed from curl will
         * abort further download*/
        if (priv->abortdl) {
            gf_msg(this->name, GF_LOG_ERROR, 0, 0, "aborting download");
            pthread_spin_unlock(&(priv->lock));
            return 0;
        }
    }
    pthread_spin_unlock(&(priv->lock));

    local = frame->local;
    dlfd = local->dlfd;
    tsize = size * nitems;

    dliov.iov_base = (void *)dlbuf;
    dliov.iov_len = tsize;

    ret = iobuf_copy(this->ctx->iobuf_pool, &dliov, 1, &iobref, &iobuf, &iov);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, 0, "iobuf_copy failed");
        goto out;
    }

    /* copy frame */
    dlframe = copy_frame(frame);
    if (!dlframe) {
        gf_msg(this->name, GF_LOG_ERROR, 0, 0, "copy_frame failed");
        tsize = 0;
        goto out;
    }

    STACK_WIND(dlframe, aws_dlwritev_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->writev, dlfd, &iov, 1, local->dloffset,
               0, iobref, NULL);

    local->dloffset += tsize;

out:
    if (iobuf)
        iobuf_unref(iobuf);
    if (iobref)
        iobref_unref(iobref);

    return tsize;
}

int
aws_download_s3(call_frame_t *frame, void *config)
{
    char *buf;
    int bufsize = -1;
    CURL *handle = NULL;
    struct curl_slist *slist = NULL;
    struct curl_slist *tmp = NULL;
    xlator_t *this = NULL;
    int ret = 0;
    int debug = 1;
    CURLcode res;
    char errbuf[CURL_ERROR_SIZE];
    size_t len = 0;
    long responsecode;
    char *sign_req = NULL;
    char *date = NULL;
    char *const reqtype = "GET";
    char *signature = NULL;
    cs_local_t *local = NULL;
    char resource[RESOURCE_SIZE] = {
        0,
    };
    aws_private_t *priv = NULL;

    this = frame->this;

    local = frame->local;

    priv = (aws_private_t *)config;

    if (!priv->bucketid || !priv->hostname || !priv->awssekey ||
        !priv->awskeyid) {
        ret = -1;
        goto out;
    }

    sign_req = aws_form_request(resource, &date, reqtype, priv->bucketid,
                                local->remotepath);
    if (!sign_req) {
        gf_msg(this->name, GF_LOG_ERROR, 0, 0,
               "null sign_req, "
               "aborting download");
        ret = -1;
        goto out;
    }

    gf_msg_debug("CS", 0, "sign_req %s date %s", sign_req, date);

    signature = aws_sign_request(sign_req, priv->awssekey);
    if (!signature) {
        gf_msg("CS", GF_LOG_ERROR, 0, 0,
               "null signature, "
               "aborting download");
        ret = -1;
        goto out;
    }

    handle = curl_easy_init();
    this = frame->this;

    /* special numbers 6, 20, 10 accounts for static characters in the
     * below snprintf string format arguments*/
    bufsize = strlen(date) + 6 + strlen(priv->awskeyid) + strlen(signature) +
              20 + strlen(priv->hostname) + 10;

    buf = (char *)alloca(bufsize);
    if (!buf) {
        gf_msg("CS", GF_LOG_ERROR, ENOMEM, 0,
               "mem allocation "
               "failed for buf");
        ret = -1;
        goto out;
    }

    snprintf(buf, bufsize, "Date: %s", date);
    slist = curl_slist_append(slist, buf);
    snprintf(buf, bufsize, "Authorization: AWS %s:%s", priv->awskeyid,
             signature);
    slist = curl_slist_append(slist, buf);
    snprintf(buf, bufsize, "https://%s/%s", priv->hostname, resource);

    if (gf_log_get_loglevel() >= GF_LOG_DEBUG) {
        tmp = slist;
        while (tmp) {
            gf_msg_debug(this->name, 0, "slist for curl - %s", tmp->data);
            tmp = tmp->next;
        }
    }

    curl_easy_setopt(handle, CURLOPT_HTTPHEADER, slist);
    curl_easy_setopt(handle, CURLOPT_URL, buf);
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, aws_write_callback);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, frame);
    curl_easy_setopt(handle, CURLOPT_VERBOSE, debug);
    curl_easy_setopt(handle, CURLOPT_ERRORBUFFER, errbuf);

    res = curl_easy_perform(handle);
    if (res != CURLE_OK) {
        gf_msg(this->name, GF_LOG_ERROR, 0, 0, "download failed. err: %s\n",
               curl_easy_strerror(res));
        ret = -1;
        len = strlen(errbuf);
        if (len) {
            gf_msg(this->name, GF_LOG_ERROR, 0, 0, "curl failure %s", errbuf);
        } else {
            gf_msg(this->name, GF_LOG_ERROR, 0, 0,
                   "curl error "
                   "%s\n",
                   curl_easy_strerror(res));
        }
    }

    if (res == CURLE_OK) {
        curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &responsecode);
        gf_msg_debug(this->name, 0, "response code %ld", responsecode);
        if (responsecode != 200) {
            ret = -1;
            gf_msg(this->name, GF_LOG_ERROR, 0, 0, "curl download failed");
        }
    }

    curl_slist_free_all(slist);
    curl_easy_cleanup(handle);

out:
    if (sign_req)
        GF_FREE(sign_req);
    if (date)
        GF_FREE(date);
    if (signature)
        GF_FREE(signature);

    return ret;
}

struct volume_options cs_options[] = {
    {.key = {"s3plugin-seckey"},
     .type = GF_OPTION_TYPE_STR,
     .description = "aws secret key"},
    {.key = {"s3plugin-keyid"},
     .type = GF_OPTION_TYPE_STR,
     .description = "aws key ID"

    },
    {.key = {"s3plugin-bucketid"},
     .type = GF_OPTION_TYPE_STR,
     .description = "aws bucketid"},
    {.key = {"s3plugin-hostname"},
     .type = GF_OPTION_TYPE_STR,
     .description = "aws hostname e.g. s3.amazonaws.com"},
    {.key = {NULL}},
};
