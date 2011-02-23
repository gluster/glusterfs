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

#include <dlfcn.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/poll.h>
#include <fnmatch.h>
#include <stdint.h>

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "logging.h"
#include "rpc-transport.h"
#include "glusterfs.h"
/* FIXME: xlator.h is needed for volume_option_t, need to define the datatype
 * in some other header
 */
#include "xlator.h"
#include "list.h"

#ifndef GF_OPTION_LIST_EMPTY
#define GF_OPTION_LIST_EMPTY(_opt) (_opt->value[0] == NULL)
#endif

int
__volume_option_value_validate (char *name,
                                data_pair_t *pair,
                                volume_option_t *opt)
{
	int       i = 0;
	int       ret = -1;
	uint64_t  input_size = 0;
	long long inputll = 0;

	/* Key is valid, validate the option */
	switch (opt->type) {
        case GF_OPTION_TYPE_XLATOR:
                break;

	case GF_OPTION_TYPE_PATH:
	{
                if (strstr (pair->value->data, "../")) {
                        gf_log (name, GF_LOG_ERROR,
                                "invalid path given '%s'",
                                pair->value->data);
                        ret = -1;
                        goto out;
                }

                /* Make sure the given path is valid */
		if (pair->value->data[0] != '/') {
			gf_log (name, GF_LOG_WARNING,
				"option %s %s: '%s' is not an "
				"absolute path name",
				pair->key, pair->value->data,
				pair->value->data);
		}
		ret = 0;
	}
	break;
	case GF_OPTION_TYPE_INT:
	{
		/* Check the range */
		if (gf_string2longlong (pair->value->data,
					&inputll) != 0) {
			gf_log (name, GF_LOG_ERROR,
				"invalid number format \"%s\" in "
				"\"option %s\"",
				pair->value->data, pair->key);
			goto out;
		}

		if ((opt->min == 0) && (opt->max == 0)) {
			gf_log (name, GF_LOG_DEBUG,
				"no range check required for "
				"'option %s %s'",
				pair->key, pair->value->data);
			ret = 0;
			break;
		}
		if ((inputll < opt->min) ||
		    (inputll > opt->max)) {
			gf_log (name, GF_LOG_WARNING,
				"'%lld' in 'option %s %s' is out of "
				"range [%"PRId64" - %"PRId64"]",
				inputll, pair->key,
				pair->value->data,
				opt->min, opt->max);
		}
		ret = 0;
	}
	break;
	case GF_OPTION_TYPE_SIZET:
	{
		/* Check the range */
		if (gf_string2bytesize (pair->value->data,
					&input_size) != 0) {
			gf_log (name, GF_LOG_ERROR,
				"invalid size format \"%s\" in "
				"\"option %s\"",
				pair->value->data, pair->key);
			goto out;
		}

		if ((opt->min == 0) && (opt->max == 0)) {
			gf_log (name, GF_LOG_DEBUG,
				"no range check required for "
				"'option %s %s'",
				pair->key, pair->value->data);
			ret = 0;
			break;
		}
		if ((input_size < opt->min) ||
		    (input_size > opt->max)) {
			gf_log (name, GF_LOG_ERROR,
				"'%"PRId64"' in 'option %s %s' is "
				"out of range [%"PRId64" - %"PRId64"]",
				input_size, pair->key,
				pair->value->data,
				opt->min, opt->max);
		}
		ret = 0;
	}
	break;
	case GF_OPTION_TYPE_BOOL:
	{
		/* Check if the value is one of
		   '0|1|on|off|no|yes|true|false|enable|disable' */
		gf_boolean_t bool_value;
		if (gf_string2boolean (pair->value->data,
				       &bool_value) != 0) {
			gf_log (name, GF_LOG_ERROR,
				"option %s %s: '%s' is not a valid "
				"boolean value",
				pair->key, pair->value->data,
				pair->value->data);
			goto out;
		}
		ret = 0;
	}
	break;
	case GF_OPTION_TYPE_STR:
	{
		/* Check if the '*str' is valid */
                if (GF_OPTION_LIST_EMPTY(opt)) {
                        ret = 0;
                        goto out;
                }

		for (i = 0; (i < ZR_OPTION_MAX_ARRAY_SIZE) &&
			     opt->value[i]; i++) {
			if (strcasecmp (opt->value[i],
					pair->value->data) == 0) {
				ret = 0;
				break;
			}
		}

		if ((i == ZR_OPTION_MAX_ARRAY_SIZE)
		    || ((i < ZR_OPTION_MAX_ARRAY_SIZE)
			&& (!opt->value[i]))) {
			/* enter here only if
			 * 1. reached end of opt->value array and haven't
                         *    validated input
			 *                      OR
			 * 2. valid input list is less than
                         *    ZR_OPTION_MAX_ARRAY_SIZE and input has not
                         *    matched all possible input values.
			 */
			char given_array[4096] = {0,};
			for (i = 0; (i < ZR_OPTION_MAX_ARRAY_SIZE) &&
				     opt->value[i]; i++) {
				strcat (given_array, opt->value[i]);
				strcat (given_array, ", ");
			}

			gf_log (name, GF_LOG_ERROR,
				"option %s %s: '%s' is not valid "
				"(possible options are %s)",
				pair->key, pair->value->data,
				pair->value->data, given_array);

			goto out;
		}
	}
	break;
	case GF_OPTION_TYPE_PERCENT:
	{
		uint32_t percent = 0;


		/* Check if the value is valid percentage */
		if (gf_string2percent (pair->value->data,
				       &percent) != 0) {
			gf_log (name, GF_LOG_ERROR,
				"invalid percent format \"%s\" "
				"in \"option %s\"",
				pair->value->data, pair->key);
			goto out;
		}

		if ((percent < 0) || (percent > 100)) {
			gf_log (name, GF_LOG_ERROR,
				"'%d' in 'option %s %s' is out of "
				"range [0 - 100]",
				percent, pair->key,
				pair->value->data);
		}
		ret = 0;
	}
	break;
	case GF_OPTION_TYPE_PERCENT_OR_SIZET:
	{
		uint32_t percent = 0;
		uint64_t input_size = 0;

		/* Check if the value is valid percentage */
		if (gf_string2percent (pair->value->data,
				       &percent) == 0) {
			if (percent > 100) {
				gf_log (name, GF_LOG_DEBUG,
					"value given was greater than 100, "
					"assuming this is actually a size");
		        if (gf_string2bytesize (pair->value->data,
				                &input_size) == 0) {
				        /* Check the range */
				if ((opt->min == 0) &&
                                            (opt->max == 0)) {
				        gf_log (name, GF_LOG_DEBUG,
				        "no range check "
                                                        "required for "
					"'option %s %s'",
				pair->key,
                                                        pair->value->data);
						// It is a size
			                        ret = 0;
				                goto out;
				}
			if ((input_size < opt->min) ||
				            (input_size > opt->max)) {
				        gf_log (name, GF_LOG_ERROR,
				        "'%"PRId64"' in "
                                                        "'option %s %s' is out"
					" of range [%"PRId64""
                                                        "- %"PRId64"]",
				input_size, pair->key,
				pair->value->data,
				                opt->min, opt->max);
				}
					// It is a size
					ret = 0;
					goto out;
				} else {
					// It's not a percent or size
					gf_log (name, GF_LOG_ERROR,
					"invalid number format \"%s\" "
					"in \"option %s\"",
					pair->value->data, pair->key);
				}

			}
			// It is a percent
			ret = 0;
			goto out;
		} else {
		        if (gf_string2bytesize (pair->value->data,
				        &input_size) == 0) {
			        /* Check the range */
			if ((opt->min == 0) && (opt->max == 0)) {
			        gf_log (name, GF_LOG_DEBUG,
				        "no range check required for "
					"'option %s %s'",
			pair->key, pair->value->data);
					// It is a size
		                        ret = 0;
				        goto out;
			}
		        if ((input_size < opt->min) ||
			            (input_size > opt->max)) {
					gf_log (name, GF_LOG_ERROR,
				        "'%"PRId64"' in 'option %s %s'"
                                                " is out of range [%"PRId64" -"
                                                " %"PRId64"]",
			input_size, pair->key,
			pair->value->data,
			                opt->min, opt->max);
				}
			} else {
				// It's not a percent or size
				gf_log (name, GF_LOG_ERROR,
					"invalid number format \"%s\" "
					"in \"option %s\"",
					pair->value->data, pair->key);
			}
			//It is a size
                        ret = 0;
		        goto out;
		}

	}
	break;
	case GF_OPTION_TYPE_TIME:
	{
		uint32_t input_time = 0;

		/* Check if the value is valid percentage */
		if (gf_string2time (pair->value->data,
				    &input_time) != 0) {
			gf_log (name,
				GF_LOG_ERROR,
				"invalid time format \"%s\" in "
				"\"option %s\"",
				pair->value->data, pair->key);
			goto out;
		}

		if ((opt->min == 0) && (opt->max == 0)) {
			gf_log (name, GF_LOG_DEBUG,
				"no range check required for "
				"'option %s %s'",
				pair->key, pair->value->data);
			ret = 0;
			goto out;
		}
		if ((input_time < opt->min) ||
		    (input_time > opt->max)) {
			gf_log (name, GF_LOG_ERROR,
				"'%"PRIu32"' in 'option %s %s' is "
				"out of range [%"PRId64" - %"PRId64"]",
				input_time, pair->key,
				pair->value->data,
				opt->min, opt->max);
		}
		ret = 0;
	}
	break;
	case GF_OPTION_TYPE_DOUBLE:
	{
		double input_time = 0.0;

		/* Check if the value is valid double */
		if (gf_string2double (pair->value->data,
				      &input_time) != 0) {
			gf_log (name,
				GF_LOG_ERROR,
				"invalid time format \"%s\" in \"option %s\"",
				pair->value->data, pair->key);
			goto out;
		}

		if (input_time < 0.0) {
			gf_log (name,
				GF_LOG_ERROR,
				"invalid time format \"%s\" in \"option %s\"",
				pair->value->data, pair->key);
			goto out;
		}

		if ((opt->min == 0) && (opt->max == 0)) {
			gf_log (name, GF_LOG_DEBUG,
				"no range check required for 'option %s %s'",
				pair->key, pair->value->data);
			ret = 0;
			goto out;
		}
		ret = 0;
	}
	break;
        case GF_OPTION_TYPE_INTERNET_ADDRESS:
        {
                if (valid_internet_address (pair->value->data)) {
                        ret = 0;
                }
	}
        break;
	case GF_OPTION_TYPE_ANY:
		/* NO CHECK */
		ret = 0;
		break;
	}

out:
	return ret;
}

/* FIXME: this procedure should be removed from transport */
int
validate_volume_options (char *name, dict_t *options, volume_option_t *opt)
{
	int i = 0;
	int ret = -1;
	int index = 0;
	volume_option_t *trav  = NULL;
	data_pair_t     *pairs = NULL;

	if (!opt) {
		ret = 0;
		goto out;
	}

	/* First search for not supported options, if any report error */
	pairs = options->members_list;
	while (pairs) {
		ret = -1;
		for (index = 0;
		     opt[index].key && opt[index].key[0] ; index++) {
			trav = &(opt[index]);
			for (i = 0 ;
			     (i < ZR_VOLUME_MAX_NUM_KEY) &&
				     trav->key[i]; i++) {
				/* Check if the key is valid */
				if (fnmatch (trav->key[i],
					     pairs->key, FNM_NOESCAPE) == 0) {
					ret = 0;
					break;
				}
			}
			if (!ret) {
				if (i) {
					gf_log (name, GF_LOG_WARNING,
						"option '%s' is deprecated, "
						"preferred is '%s', continuing"
						" with correction",
						trav->key[i], trav->key[0]);
					/* TODO: some bytes lost */
					pairs->key = gf_strdup (trav->key[0]);
				}
				break;
			}
		}
		if (!ret) {
			ret = __volume_option_value_validate (name, pairs, trav);
			if (-1 == ret) {
				goto out;
			}
		}

		pairs = pairs->next;
	}

	ret = 0;
 out:
	return ret;
}

int32_t
rpc_transport_get_myaddr (rpc_transport_t *this, char *peeraddr, int addrlen,
                          struct sockaddr_storage *sa, size_t salen)
{
        if (!this)
                return -1;

        return this->ops->get_myaddr (this, peeraddr, addrlen, sa, salen);
}

int32_t
rpc_transport_get_myname (rpc_transport_t *this, char *hostname, int hostlen)
{
        if (!this)
                return -1;

        return this->ops->get_myname (this, hostname, hostlen);
}

int32_t
rpc_transport_get_peername (rpc_transport_t *this, char *hostname, int hostlen)
{
        if (!this)
                return -1;
        return this->ops->get_peername (this, hostname, hostlen);
}

int32_t
rpc_transport_get_peeraddr (rpc_transport_t *this, char *peeraddr, int addrlen,
                            struct sockaddr_storage *sa, size_t salen)
{
        if (!this)
                return -1;
        return this->ops->get_peeraddr (this, peeraddr, addrlen, sa, salen);
}

void
rpc_transport_pollin_destroy (rpc_transport_pollin_t *pollin)
{
        if (!pollin) {
                goto out;
        }

        if (pollin->iobref) {
                iobref_unref (pollin->iobref);
        }
 
        if (pollin->hdr_iobuf) {
                iobuf_unref (pollin->hdr_iobuf);
        }

        if (pollin->private) {
                /* */
                GF_FREE (pollin->private);
        }

        GF_FREE (pollin);
out:
        return;
}


rpc_transport_pollin_t *
rpc_transport_pollin_alloc (rpc_transport_t *this, struct iovec *vector,
                            int count, struct iobuf *hdr_iobuf,
                            struct iobref *iobref, void *private)
{
        rpc_transport_pollin_t *msg = NULL;
        msg = GF_CALLOC (1, sizeof (*msg), gf_common_mt_rpc_trans_pollin_t);
        if (!msg) {
                gf_log ("rpc-transport", GF_LOG_ERROR, "out of memory");
                goto out;
        }

        if (count > 1) {
                msg->vectored = 1;
        }

        memcpy (msg->vector, vector, count * sizeof (*vector));
        msg->count = count;
        msg->iobref = iobref_ref (iobref);
        msg->private = private;
        msg->hdr_iobuf = iobuf_ref (hdr_iobuf);

out:
        return msg;
}


rpc_transport_pollin_t *
rpc_transport_same_process_pollin_alloc (rpc_transport_t *this,
                                         struct iovec *rpchdr, int rpchdrcount,
                                         struct iovec *proghdr,
                                         int proghdrcount,
                                         struct iovec *progpayload,
                                         int progpayloadcount,
                                         rpc_transport_rsp_t *rsp,
                                         char is_request)
{
        rpc_transport_pollin_t *msg            = NULL;
        int                     rpchdrlen      = 0, proghdrlen = 0;
        int                     progpayloadlen = 0;
        char                    vectored       = 0;
        char                   *hdr            = NULL, *progpayloadbuf = NULL;
        struct iobuf           *iobuf          = NULL;

        if (!rpchdr || !proghdr) {
                goto err;
        }

        msg = GF_CALLOC (1, sizeof (*msg), gf_common_mt_rpc_trans_pollin_t);
        if (!msg) {
                gf_log ("rpc-transport", GF_LOG_ERROR, "out of memory");
                goto err;
        }

        rpchdrlen = iov_length (rpchdr, rpchdrcount);
        proghdrlen = iov_length (proghdr, proghdrcount);

        if (progpayload) {
                vectored = 1;
                progpayloadlen = iov_length (progpayload, progpayloadcount);
        }

        /* FIXME: we are assuming rpchdr and proghdr will fit into
         * an iobuf (128KB)
         */
        if ((rpchdrlen + proghdrlen) > this->ctx->page_size) {
                gf_log ("rpc_transport", GF_LOG_DEBUG, "program hdr and rpc"
                        " hdr together combined (%d) is bigger than "
                        "iobuf size (%zu)", (rpchdrlen + proghdrlen),
                        this->ctx->page_size);
                goto err;
        }

        if (vectored) {
                msg->iobref = iobref_new ();
                if (!msg->iobref) {
                        gf_log ("rpc-transport", GF_LOG_ERROR,
                                "out of memory");
                        goto err;
                }

                iobuf = iobuf_get (this->ctx->iobuf_pool);
                if (!iobuf) {
                        gf_log ("rpc_transport", GF_LOG_ERROR,
                                "out of memory");
                        goto err;
                }

                iobref_add (msg->iobref, iobuf);
                iobuf_unref (iobuf);

                msg->vector[0].iov_len = rpchdrlen + proghdrlen;
                msg->vector[0].iov_base = hdr = iobuf_ptr (iobuf);

                if (!is_request && rsp) {
                        msg->vector[1] = rsp->rsp_payload[0];
                        progpayloadbuf = rsp->rsp_payload[0].iov_base;
                } else {
                        iobuf = iobuf_get (this->ctx->iobuf_pool);
                        if (!iobuf) {
                                gf_log ("rpc_transport", GF_LOG_ERROR,
                                        "out of memory");
                                goto err;
                        }

                        iobref_add (msg->iobref, iobuf);
                        iobuf_unref (iobuf);
 
                        msg->vector[1].iov_base
                                = progpayloadbuf = iobuf_ptr (iobuf);
                }
                msg->vector[1].iov_len = progpayloadlen;
        } else {
                if (!is_request && rsp) {
                        /* FIXME: Assuming rspvec contains only one vector */
                        hdr = rsp->rsphdr[0].iov_base;
                        msg->vector[0] = rsp->rsphdr[0];
                } else {
                        msg->iobref = iobref_new ();
                        if (!msg->iobref) {
                                gf_log ("rpc-transport", GF_LOG_ERROR,
                                        "out of memory");
                                goto err;
                        }

                        iobuf = iobuf_get (this->ctx->iobuf_pool);
                        if (!iobuf) {
                                gf_log ("rpc_transport", GF_LOG_ERROR,
                                        "out of memory");
                                goto err;
                        }

                        iobref_add (msg->iobref, iobuf);
                        iobuf_unref (iobuf);

                        hdr = iobuf_ptr (iobuf);
                        msg->vector[0].iov_base = hdr;
                }

                msg->vector[0].iov_len = rpchdrlen + proghdrlen;
        }

        iov_unload (hdr, rpchdr, rpchdrcount);
        hdr += rpchdrlen;
        iov_unload (hdr, proghdr, proghdrcount);

        if (progpayload) {
                iov_unload (progpayloadbuf, progpayload,
                            progpayloadcount);
        }

        if (is_request) {
                msg->private = rsp;
        }
        return msg;
err:
        if (msg) {
                rpc_transport_pollin_destroy (msg);
        }

        return NULL;
}



rpc_transport_t *
rpc_transport_load (glusterfs_ctx_t *ctx, dict_t *options, char *trans_name)
{
	struct rpc_transport *trans = NULL, *return_trans = NULL;
	char *name = NULL;
	void *handle = NULL;
	char *type = NULL;
	char str[] = "ERROR";
	int32_t ret = -1;
	int8_t is_tcp = 0, is_unix = 0, is_ibsdp = 0;
	volume_opt_list_t *vol_opt = NULL;

	GF_VALIDATE_OR_GOTO("rpc-transport", options, fail);
	GF_VALIDATE_OR_GOTO("rpc-transport", ctx, fail);
	GF_VALIDATE_OR_GOTO("rpc-transport", trans_name, fail);

	trans = GF_CALLOC (1, sizeof (struct rpc_transport), gf_common_mt_rpc_trans_t);
	GF_VALIDATE_OR_GOTO("rpc-transport", trans, fail);

        trans->name = gf_strdup (trans_name);
        GF_VALIDATE_OR_GOTO ("rpc-transport", trans->name, fail);

	trans->ctx = ctx;
	type = str;

	/* Backward compatibility */
        ret = dict_get_str (options, "transport-type", &type);
	if (ret < 0) {
		ret = dict_set_str (options, "transport-type", "socket");
		if (ret < 0)
			gf_log ("dict", GF_LOG_DEBUG,
				"setting transport-type failed");
		gf_log ("rpc-transport", GF_LOG_WARNING,
			"missing 'option transport-type'. defaulting to "
			"\"socket\"");
	} else {
		{
			/* Backword compatibility to handle * /client,
			 * * /server.
			 */
			char *tmp = strchr (type, '/');
			if (tmp)
				*tmp = '\0';
		}

		is_tcp = strcmp (type, "tcp");
		is_unix = strcmp (type, "unix");
		is_ibsdp = strcmp (type, "ib-sdp");
		if ((is_tcp == 0) ||
		    (is_unix == 0) ||
		    (is_ibsdp == 0)) {
			if (is_unix == 0)
				ret = dict_set_str (options,
						    "transport.address-family",
						    "unix");
			if (is_ibsdp == 0)
				ret = dict_set_str (options,
						    "transport.address-family",
						    "inet-sdp");

			if (ret < 0)
				gf_log ("dict", GF_LOG_DEBUG,
					"setting address-family failed");

			ret = dict_set_str (options,
					    "transport-type", "socket");
			if (ret < 0)
				gf_log ("dict", GF_LOG_DEBUG,
					"setting transport-type failed");
		}
	}

	ret = dict_get_str (options, "transport-type", &type);
	if (ret < 0) {
		gf_log ("rpc-transport", GF_LOG_ERROR,
			"'option transport-type <xx>' missing in volume '%s'",
			trans_name);
		goto fail;
	}

	ret = gf_asprintf (&name, "%s/%s.so", RPC_TRANSPORTDIR, type);
        if (-1 == ret) {
                gf_log ("rpc-transport", GF_LOG_ERROR, "asprintf failed");
                goto fail;
        }
	gf_log ("rpc-transport", GF_LOG_DEBUG,
		"attempt to load file %s", name);

	handle = dlopen (name, RTLD_NOW|RTLD_GLOBAL);
	if (handle == NULL) {
		gf_log ("rpc-transport", GF_LOG_ERROR, "%s", dlerror ());
		gf_log ("rpc-transport", GF_LOG_ERROR,
			"volume '%s': transport-type '%s' is not valid or "
			"not found on this machine",
			trans_name, type);
		goto fail;
	}

	trans->ops = dlsym (handle, "tops");
	if (trans->ops == NULL) {
		gf_log ("rpc-transport", GF_LOG_ERROR,
			"dlsym (rpc_transport_ops) on %s", dlerror ());
		goto fail;
	}

	trans->init = dlsym (handle, "init");
	if (trans->init == NULL) {
		gf_log ("rpc-transport", GF_LOG_ERROR,
			"dlsym (gf_rpc_transport_init) on %s", dlerror ());
		goto fail;
	}

	trans->fini = dlsym (handle, "fini");
	if (trans->fini == NULL) {
		gf_log ("rpc-transport", GF_LOG_ERROR,
			"dlsym (gf_rpc_transport_fini) on %s", dlerror ());
		goto fail;
	}
        
        trans->reconfigure = dlsym (handle, "reconfigure");
        if (trans->fini == NULL) {
                gf_log ("rpc-transport", GF_LOG_DEBUG,
                        "dlsym (gf_rpc_transport_reconfigure) on %s", dlerror());
        }

	vol_opt = GF_CALLOC (1, sizeof (volume_opt_list_t),
                             gf_common_mt_volume_opt_list_t);
        if (!vol_opt) {
                gf_log (trans_name, GF_LOG_ERROR, "out of memory");
                goto fail;
        }

	vol_opt->given_opt = dlsym (handle, "options");
	if (vol_opt->given_opt == NULL) {
		gf_log ("rpc-transport", GF_LOG_DEBUG,
			"volume option validation not specified");
	} else {
                /* FIXME: is adding really needed? */
		/* list_add_tail (&vol_opt->list, &xl->volume_options); */
		if (-1 ==
		    validate_volume_options (trans_name, options,
                                             vol_opt->given_opt)) {
			gf_log ("rpc-transport", GF_LOG_ERROR,
				"volume option validation failed");
			goto fail;
		}
	}

        trans->options = options;

        pthread_mutex_init (&trans->lock, NULL);
        trans->xl = THIS;

	ret = trans->init (trans);
	if (ret != 0) {
		gf_log ("rpc-transport", GF_LOG_ERROR,
			"'%s' initialization failed", type);
		goto fail;
	}

	return_trans = trans;

        if (name) {
                GF_FREE (name);
        }

        GF_FREE (vol_opt);
	return return_trans;

fail:
        if (trans) {
                if (trans->name) {
                        GF_FREE (trans->name);
                }

                GF_FREE (trans);
        }

        if (vol_opt) {
                GF_FREE (vol_opt);
        }

        if (name) {
                GF_FREE (name);
        }

        return NULL;
}


int32_t
rpc_transport_submit_request (rpc_transport_t *this, rpc_transport_req_t *req)
{
	int32_t                       ret          = -1;

	GF_VALIDATE_OR_GOTO("rpc_transport", this, fail);
	GF_VALIDATE_OR_GOTO("rpc_transport", this->ops, fail);

	ret = this->ops->submit_request (this, req);
fail:
	return ret;
}


int32_t
rpc_transport_submit_reply (rpc_transport_t *this, rpc_transport_reply_t *reply)
{
	int32_t                   ret          = -1;

	GF_VALIDATE_OR_GOTO("rpc_transport", this, fail);
	GF_VALIDATE_OR_GOTO("rpc_transport", this->ops, fail);

	ret = this->ops->submit_reply (this, reply);
fail:
	return ret;
}


int32_t
rpc_transport_connect (rpc_transport_t *this, int port)
{
	int ret = -1;

	GF_VALIDATE_OR_GOTO("rpc_transport", this, fail);

	ret = this->ops->connect (this, port);
fail:
	return ret;
}


int32_t
rpc_transport_listen (rpc_transport_t *this)
{
	int ret = -1;

	GF_VALIDATE_OR_GOTO("rpc_transport", this, fail);

	ret = this->ops->listen (this);
fail:
	return ret;
}


int32_t
rpc_transport_disconnect (rpc_transport_t *this)
{
	int32_t ret = -1;

	GF_VALIDATE_OR_GOTO("rpc_transport", this, fail);

	ret = this->ops->disconnect (this);
fail:
	return ret;
}


int32_t
rpc_transport_destroy (rpc_transport_t *this)
{
	int32_t ret = -1;

	GF_VALIDATE_OR_GOTO("rpc_transport", this, fail);

	if (this->fini)
		this->fini (this);

	pthread_mutex_destroy (&this->lock);

        if (this->name)
                GF_FREE (this->name);

	GF_FREE (this);
fail:
	return ret;
}


rpc_transport_t *
rpc_transport_ref (rpc_transport_t *this)
{
	rpc_transport_t *return_this = NULL;

	GF_VALIDATE_OR_GOTO("rpc_transport", this, fail);

	pthread_mutex_lock (&this->lock);
	{
		this->refcount ++;
	}
	pthread_mutex_unlock (&this->lock);

	return_this = this;
fail:
	return return_this;
}


int32_t
rpc_transport_unref (rpc_transport_t *this)
{
	int32_t refcount = 0;
	int32_t ret = -1;

	GF_VALIDATE_OR_GOTO("rpc_transport", this, fail);

	pthread_mutex_lock (&this->lock);
	{
		refcount = --this->refcount;
	}
	pthread_mutex_unlock (&this->lock);

	if (refcount == 0) {
                if (this->mydata)
                        this->notify (this, this->mydata, RPC_TRANSPORT_CLEANUP, NULL);
		rpc_transport_destroy (this);
	}

	ret = 0;
fail:
	return ret;
}


int32_t
rpc_transport_notify (rpc_transport_t *this, rpc_transport_event_t event,
                      void *data, ...)
{
        int32_t ret = -1;

        if (this == NULL) {
                goto out;
        }

        if (this->notify != NULL) {
                ret = this->notify (this, this->mydata, event, data);
        } else {
                ret = 0;
        }
out:
        return ret;
}



inline int
rpc_transport_register_notify (rpc_transport_t *trans,
                               rpc_transport_notify_t notify, void *mydata)
{
        int ret = -1;

        if (trans == NULL) {
                goto out;
        }

        trans->notify = notify;
        trans->mydata = mydata;

        ret = 0;
out:
        return ret;
}
