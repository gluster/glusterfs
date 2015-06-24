/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <fnmatch.h>

#include "stripe.h"
#include "byte-order.h"
#include "mem-types.h"
#include "logging.h"

void
stripe_local_wipe (stripe_local_t *local)
{
        if (!local)
                goto out;

        loc_wipe (&local->loc);
        loc_wipe (&local->loc2);

        if (local->fd)
                fd_unref (local->fd);

        if (local->inode)
                inode_unref (local->inode);

        if (local->xattr)
                dict_unref (local->xattr);

        if (local->xdata)
                dict_unref (local->xdata);

out:
        return;
}



int
stripe_aggregate (dict_t *this, char *key, data_t *value, void *data)
{
        dict_t  *dst  = NULL;
        int64_t *ptr  = 0, *size = NULL;
        int32_t  ret  = -1;

        dst = data;

        if (strcmp (key, QUOTA_SIZE_KEY) == 0) {
                ret = dict_get_bin (dst, key, (void **)&size);
                if (ret < 0) {
                        size = GF_CALLOC (1, sizeof (int64_t),
                                          gf_common_mt_char);
                        if (size == NULL) {
                                gf_log ("stripe", GF_LOG_WARNING,
                                        "memory allocation failed");
                                goto out;
                        }
                        ret = dict_set_bin (dst, key, size, sizeof (int64_t));
                        if (ret < 0) {
                                gf_log ("stripe", GF_LOG_WARNING,
                                        "stripe aggregate dict set failed");
                                GF_FREE (size);
                                goto out;
                        }
                }

                ptr = data_to_bin (value);
                if (ptr == NULL) {
                        gf_log ("stripe", GF_LOG_WARNING, "data to bin failed");
                        goto out;
                }

                *size = hton64 (ntoh64 (*size) + ntoh64 (*ptr));
        } else if (strcmp (key, GF_CONTENT_KEY)) {
                /* No need to aggregate 'CONTENT' data */
                ret = dict_set (dst, key, value);
                if (ret)
                        gf_log ("stripe", GF_LOG_WARNING, "xattr dict set failed");
        }

out:
        return 0;
}


void
stripe_aggregate_xattr (dict_t *dst, dict_t *src)
{
        if ((dst == NULL) || (src == NULL)) {
                goto out;
        }

        dict_foreach (src, stripe_aggregate, dst);
out:
        return;
}


int32_t
stripe_xattr_aggregate (char *buffer, stripe_local_t *local, int32_t *total)
{
        int32_t              i     = 0;
        int32_t              ret   = -1;
        int32_t              len   = 0;
        char                *sbuf  = NULL;
        stripe_xattr_sort_t *xattr = NULL;

        if (!buffer || !local || !local->xattr_list)
                goto out;

        sbuf = buffer;

        for (i = 0; i < local->nallocs; i++) {
                xattr = local->xattr_list + i;
                len = xattr->xattr_len - 1; /* length includes \0 */

                if (len && xattr && xattr->xattr_value) {
                        memcpy (buffer, xattr->xattr_value, len);
                        buffer += len;
                        *buffer++ = ' ';
                }
        }

        *--buffer = '\0';
        if (total)
                *total = buffer - sbuf;
        ret = 0;

 out:
        return ret;
}

int32_t
stripe_free_xattr_str (stripe_local_t *local)
{
        int32_t              i     = 0;
        int32_t              ret   = -1;
        stripe_xattr_sort_t *xattr = NULL;

        if (!local || !local->xattr_list)
                goto out;

        for (i = 0; i < local->nallocs; i++) {
                xattr = local->xattr_list + i;

                if (xattr && xattr->xattr_value)
                        GF_FREE (xattr->xattr_value);
        }

        ret = 0;
 out:
        return ret;
}


int32_t
stripe_fill_lockinfo_xattr (xlator_t *this, stripe_local_t *local,
                            void **xattr_serz)
{
        int32_t              ret   = -1, i = 0, len = 0;
        dict_t              *tmp1  = NULL, *tmp2 = NULL;
        char                *buf   = NULL;
        stripe_xattr_sort_t *xattr = NULL;

        if (xattr_serz == NULL) {
                goto out;
        }

        tmp2 = dict_new ();

        if (tmp2 == NULL) {
                goto out;
        }

        for (i = 0; i < local->nallocs; i++) {
                xattr = local->xattr_list + i;
                len = xattr->xattr_len;

                if (len && xattr && xattr->xattr_value) {
                        ret = dict_reset (tmp2);
                        if (ret < 0) {
                                gf_log (this->name, GF_LOG_DEBUG,
                                        "dict_reset failed (%s)",
                                        strerror (-ret));
                        }

                        ret = dict_unserialize (xattr->xattr_value,
                                                xattr->xattr_len,
                                                &tmp2);
                        if (ret < 0) {
                                gf_log (this->name, GF_LOG_WARNING,
                                        "dict_unserialize failed (%s)",
                                        strerror (-ret));
                                ret = -1;
                                goto out;
                        }

                        tmp1 = dict_copy (tmp2, tmp1);
                        if (tmp1 == NULL) {
                                gf_log (this->name, GF_LOG_WARNING,
                                        "dict_copy failed (%s)",
                                        strerror (-ret));
                                ret = -1;
                                goto out;
                        }
                }
        }

        len = dict_serialized_length (tmp1);
        if (len > 0) {
                buf = GF_CALLOC (1, len, gf_common_mt_dict_t);
                if (buf == NULL) {
                        ret = -1;
                        goto out;
                }

                ret = dict_serialize (tmp1, buf);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "dict_serialize failed (%s)", strerror (-ret));
                        GF_FREE(buf);
                        ret = -1;
                        goto out;
                }

                *xattr_serz = buf;
        }

        ret = 0;
out:
        if (tmp1 != NULL) {
                dict_unref (tmp1);
        }

        if (tmp2 != NULL) {
                dict_unref (tmp2);
        }

        return ret;
}


int32_t
stripe_fill_pathinfo_xattr (xlator_t *this, stripe_local_t *local,
                            char **xattr_serz)
{
        int      ret             = -1;
        int32_t  padding         = 0;
        int32_t  tlen            = 0;
        char stripe_size_str[20] = {0,};
        char    *pathinfo_serz   = NULL;

        if (!local) {
                gf_log (this->name, GF_LOG_ERROR, "Possible NULL deref");
                goto out;
        }

        (void) snprintf (stripe_size_str, 20, "%"PRId64,
                         (long long) (local->fctx) ? local->fctx->stripe_size : 0);

        /* extra bytes for decorations (brackets and <>'s) */
        padding = strlen (this->name) + strlen (STRIPE_PATHINFO_HEADER)
                + strlen (stripe_size_str) + 7;
        local->xattr_total_len += (padding + 2);

        pathinfo_serz = GF_CALLOC (local->xattr_total_len, sizeof (char),
                                   gf_common_mt_char);
        if (!pathinfo_serz)
                goto out;

        /* xlator info */
        (void) sprintf (pathinfo_serz, "(<"STRIPE_PATHINFO_HEADER"%s:[%s]> ",
                        this->name, stripe_size_str);

        ret = stripe_xattr_aggregate (pathinfo_serz + padding, local, &tlen);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Cannot aggregate pathinfo list");
                GF_FREE(pathinfo_serz);
                goto out;
        }

        *(pathinfo_serz + padding + tlen) = ')';
        *(pathinfo_serz + padding + tlen + 1) = '\0';

        *xattr_serz = pathinfo_serz;

        ret = 0;
 out:
        return ret;
}

/**
 * stripe_get_matching_bs - Get the matching block size for the given path.
 */
int32_t
stripe_get_matching_bs (const char *path, stripe_private_t *priv)
{
        struct stripe_options *trav       = NULL;
        uint64_t               block_size = 0;

        GF_VALIDATE_OR_GOTO ("stripe", priv, out);
        GF_VALIDATE_OR_GOTO ("stripe", path, out);

        LOCK (&priv->lock);
        {
                block_size = priv->block_size;
                trav = priv->pattern;
                while (trav) {
                        if (!fnmatch (trav->path_pattern, path, FNM_NOESCAPE)) {
                                block_size = trav->block_size;
                                break;
                        }
                        trav = trav->next;
                }
        }
        UNLOCK (&priv->lock);

out:
        return block_size;
}

int32_t
stripe_ctx_handle (xlator_t *this, call_frame_t *prev, stripe_local_t *local,
                   dict_t *dict)
{
        char            key[256]       = {0,};
        data_t         *data            = NULL;
        int32_t         index           = 0;
        stripe_private_t *priv          = NULL;

        priv = this->private;


        if (!local->fctx) {
                local->fctx =  GF_CALLOC (1, sizeof (stripe_fd_ctx_t),
                                         gf_stripe_mt_stripe_fd_ctx_t);
                if (!local->fctx) {
                        local->op_errno = ENOMEM;
                        local->op_ret = -1;
                        goto out;
                }

                local->fctx->static_array = 0;
        }
        /* Stripe block size */
        sprintf (key, "trusted.%s.stripe-size", this->name);
        data = dict_get (dict, key);
        if (!data) {
                local->xattr_self_heal_needed = 1;
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to get stripe-size");
                goto out;
        } else {
                if (!local->fctx->stripe_size) {
                        local->fctx->stripe_size =
                                     data_to_int64 (data);
                }

                if (local->fctx->stripe_size != data_to_int64 (data)) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "stripe-size mismatch in blocks");
                        local->xattr_self_heal_needed = 1;
                }
        }

        /* Stripe count */
        sprintf (key, "trusted.%s.stripe-count", this->name);
        data = dict_get (dict, key);

        if (!data) {
                local->xattr_self_heal_needed = 1;
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to get stripe-count");
                goto out;
        }
        if (!local->fctx->xl_array) {
                local->fctx->stripe_count = data_to_int32 (data);
                if (!local->fctx->stripe_count) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "error with stripe-count xattr");
                        local->op_ret   = -1;
                        local->op_errno = EIO;
                        goto out;
                }

                local->fctx->xl_array = GF_CALLOC (local->fctx->stripe_count,
                                                   sizeof (xlator_t *),
                                                   gf_stripe_mt_xlator_t);

                if (!local->fctx->xl_array) {
                        local->op_errno = ENOMEM;
                        local->op_ret   = -1;
                        goto out;
                }
        }
        if (local->fctx->stripe_count != data_to_int32 (data)) {
                gf_log (this->name, GF_LOG_ERROR,
                        "error with stripe-count xattr (%d != %d)",
                        local->fctx->stripe_count, data_to_int32 (data));
                local->op_ret   = -1;
                local->op_errno = EIO;
                goto out;
        }

        /* index */
        sprintf (key, "trusted.%s.stripe-index", this->name);
        data = dict_get (dict, key);
        if (!data) {
                local->xattr_self_heal_needed = 1;
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to get stripe-index");
                goto out;
        }
        index = data_to_int32 (data);
        if (index > priv->child_count) {
                gf_log (this->name, GF_LOG_ERROR,
                        "error with stripe-index xattr (%d)", index);
                local->op_ret   = -1;
                local->op_errno = EIO;
                goto out;
        }
        if (local->fctx->xl_array) {
                if (!local->fctx->xl_array[index])
                        local->fctx->xl_array[index] = prev->this;
        }

	sprintf(key, "trusted.%s.stripe-coalesce", this->name);
	data = dict_get(dict, key);
	if (!data) {
		/*
		 * The file was probably created prior to coalesce support.
		 * Assume non-coalesce mode for this file to maintain backwards
		 * compatibility.
		 */
		gf_log(this->name, GF_LOG_DEBUG, "missing stripe-coalesce "
			"attr, assume non-coalesce mode");
		local->fctx->stripe_coalesce = 0;
	} else {
		local->fctx->stripe_coalesce = data_to_int32(data);
	}


out:
        return 0;
}

int32_t
stripe_xattr_request_build (xlator_t *this, dict_t *dict, uint64_t stripe_size,
                            uint32_t stripe_count, uint32_t stripe_index,
			    uint32_t stripe_coalesce)
{
        char            key[256]       = {0,};
        int32_t         ret             = -1;

        sprintf (key, "trusted.%s.stripe-size", this->name);
        ret = dict_set_int64 (dict, key, stripe_size);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING,
                        "failed to set %s in xattr_req dict", key);
                goto out;
        }

        sprintf (key, "trusted.%s.stripe-count", this->name);
        ret = dict_set_int32 (dict, key, stripe_count);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING,
                        "failed to set %s in xattr_req dict", key);
                goto out;
        }

        sprintf (key, "trusted.%s.stripe-index", this->name);
        ret = dict_set_int32 (dict, key, stripe_index);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING,
                        "failed to set %s in xattr_req dict", key);
                goto out;
        }

	sprintf(key, "trusted.%s.stripe-coalesce", this->name);
	ret = dict_set_int32(dict, key, stripe_coalesce);
	if (ret) {
		gf_log(this->name, GF_LOG_WARNING,
			"failed to set %s in xattr_req_dict", key);
		goto out;
	}
out:
        return ret;
}


static int
set_default_block_size (stripe_private_t *priv, char *num)
{

        int             ret = -1;
        GF_VALIDATE_OR_GOTO ("stripe", THIS, out);
        GF_VALIDATE_OR_GOTO (THIS->name, priv, out);
        GF_VALIDATE_OR_GOTO (THIS->name, num, out);


        if (gf_string2bytesize_uint64 (num, &priv->block_size) != 0) {
                gf_log (THIS->name, GF_LOG_ERROR,
                        "invalid number format \"%s\"", num);
                goto out;
        }

        ret = 0;

 out:
        return ret;

}


int
set_stripe_block_size (xlator_t *this, stripe_private_t *priv, char *data)
{
        int                    ret = -1;
        char                  *tmp_str = NULL;
        char                  *tmp_str1 = NULL;
        char                  *dup_str = NULL;
        char                  *stripe_str = NULL;
        char                  *pattern = NULL;
        char                  *num = NULL;
        struct stripe_options *temp_stripeopt = NULL;
        struct stripe_options *stripe_opt = NULL;

        if (!this || !priv || !data)
                goto out;

        /* Get the pattern for striping.
           "option block-size *avi:10MB" etc */
        stripe_str = strtok_r (data, ",", &tmp_str);
        while (stripe_str) {
                dup_str = gf_strdup (stripe_str);
                stripe_opt = GF_CALLOC (1, sizeof (struct stripe_options),
                                        gf_stripe_mt_stripe_options);
                if (!stripe_opt) {
                        goto out;
                }

                pattern = strtok_r (dup_str, ":", &tmp_str1);
                num = strtok_r (NULL, ":", &tmp_str1);
                if (!num) {
                        num = pattern;
                        pattern = "*";
                        ret = set_default_block_size (priv, num);
                        if (ret)
                                goto out;
                }
                if (gf_string2bytesize_uint64 (num, &stripe_opt->block_size) != 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "invalid number format \"%s\"", num);
                        goto out;
                }

                if (stripe_opt->block_size < STRIPE_MIN_BLOCK_SIZE) {
                        gf_log (this->name, GF_LOG_ERROR, "Invalid Block-size: "
                                "%s. Should be atleast %llu bytes", num,
                                STRIPE_MIN_BLOCK_SIZE);
                        goto out;
                }
                if (stripe_opt->block_size % 512) {
                        gf_log (this->name, GF_LOG_ERROR, "Block-size: %s should"
                                " be a multiple of 512 bytes", num);
                        goto out;
                }

                memcpy (stripe_opt->path_pattern, pattern, strlen (pattern));

                gf_log (this->name, GF_LOG_DEBUG,
                        "block-size : pattern %s : size %"PRId64,
                        stripe_opt->path_pattern, stripe_opt->block_size);

                if (priv->pattern)
                        temp_stripeopt = NULL;
                else
                        temp_stripeopt = priv->pattern;

                stripe_opt->next = temp_stripeopt;

                priv->pattern = stripe_opt;
                stripe_opt = NULL;

                GF_FREE (dup_str);
                dup_str = NULL;

                stripe_str = strtok_r (NULL, ",", &tmp_str);
        }

        ret = 0;
out:

        GF_FREE (dup_str);

        GF_FREE (stripe_opt);

        return ret;
}

int32_t
stripe_iatt_merge (struct iatt *from, struct iatt *to)
{
        if (to->ia_size < from->ia_size)
                to->ia_size = from->ia_size;
        if (to->ia_mtime < from->ia_mtime)
                to->ia_mtime = from->ia_mtime;
        if (to->ia_ctime < from->ia_ctime)
                to->ia_ctime = from->ia_ctime;
        if (to->ia_atime < from->ia_atime)
                to->ia_atime = from->ia_atime;
        return 0;
}

off_t
coalesced_offset(off_t offset, uint64_t stripe_size, int stripe_count)
{
	size_t line_size = 0;
	uint64_t stripe_num = 0;
	off_t coalesced_offset = 0;

	line_size = stripe_size * stripe_count;
	stripe_num = offset / line_size;

	coalesced_offset = (stripe_num * stripe_size) +
		(offset % stripe_size);

	return coalesced_offset;
}

off_t
uncoalesced_size(off_t size, uint64_t stripe_size, int stripe_count,
		int stripe_index)
{
        uint64_t nr_full_stripe_chunks = 0, mod = 0;

        if (!size)
                return size;

	/*
	 * Estimate the number of fully written stripes from the
	 * local file size. Each stripe_size chunk corresponds to
	 * a stripe.
	 */
        nr_full_stripe_chunks = (size / stripe_size) * stripe_count;
        mod = size % stripe_size;

        if (!mod) {
                /*
		 * There is no remainder, thus we could have overestimated
		 * the size of the file in terms of chunks. Trim the number
		 * of chunks by the following stripe members and leave it
		 * up to those nodes to respond with a larger size (if
		 * necessary).
		 */
                nr_full_stripe_chunks -= stripe_count -
                        (stripe_index + 1);
                size = nr_full_stripe_chunks * stripe_size;
        } else {
		/*
		 * There is a remainder and thus we own the last chunk of the
		 * file. Add the preceding stripe members of the final stripe
		 * along with the remainder to calculate the exact size.
		 */
                nr_full_stripe_chunks += stripe_index;
                size = nr_full_stripe_chunks * stripe_size + mod;
        }

        return size;
}
