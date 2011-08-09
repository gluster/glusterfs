/*
  Copyright (c) 2010-2011 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
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
				     opt->value[i];) {
				strcat (given_array, opt->value[i]);
                                if(((++i) < ZR_OPTION_MAX_ARRAY_SIZE) &&
                                   (opt->value[i]))
				        strcat (given_array, ", ");
                                else
                                        strcat (given_array, ".");
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
                if (!valid_internet_address (pair->value->data)) {
			gf_log (name, GF_LOG_ERROR,
			        "internet address '%s' does not conform to"
				"standards.", pair->value->data);
                }
                ret = 0;
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
        int32_t ret = -1;
        GF_VALIDATE_OR_GOTO ("rpc", this, out);

        ret = this->ops->get_myaddr (this, peeraddr, addrlen, sa, salen);

out:
        return ret;
}

int32_t
rpc_transport_get_myname (rpc_transport_t *this, char *hostname, int hostlen)
{
        int32_t ret = -1;
        GF_VALIDATE_OR_GOTO ("rpc", this, out);

        ret = this->ops->get_myname (this, hostname, hostlen);
out:
        return ret;
}

int32_t
rpc_transport_get_peername (rpc_transport_t *this, char *hostname, int hostlen)
{
        int32_t ret = -1;
        GF_VALIDATE_OR_GOTO ("rpc", this, out);

        ret = this->ops->get_peername (this, hostname, hostlen);
out:
        return ret;
}

int32_t
rpc_transport_get_peeraddr (rpc_transport_t *this, char *peeraddr, int addrlen,
                            struct sockaddr_storage *sa, size_t salen)
{
        int32_t ret = -1;
        GF_VALIDATE_OR_GOTO ("rpc", this, out);

        ret = this->ops->get_peeraddr (this, peeraddr, addrlen, sa, salen);
out:
        return ret;
}

void
rpc_transport_pollin_destroy (rpc_transport_pollin_t *pollin)
{
        GF_VALIDATE_OR_GOTO ("rpc", pollin, out);

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
                goto out;
        }

        if (count > 1) {
                msg->vectored = 1;
        }

        memcpy (msg->vector, vector, count * sizeof (*vector));
        msg->count = count;
        msg->iobref = iobref_ref (iobref);
        msg->private = private;
        if (hdr_iobuf)
                msg->hdr_iobuf = iobuf_ref (hdr_iobuf);

out:
        return msg;
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
        gf_boolean_t bind_insecure = _gf_false;

	GF_VALIDATE_OR_GOTO("rpc-transport", options, fail);
	GF_VALIDATE_OR_GOTO("rpc-transport", ctx, fail);
	GF_VALIDATE_OR_GOTO("rpc-transport", trans_name, fail);

	trans = GF_CALLOC (1, sizeof (struct rpc_transport), gf_common_mt_rpc_trans_t);
        if (!trans)
                goto fail;

        trans->name = gf_strdup (trans_name);
        if (!trans->name)
                goto fail;

	trans->ctx = ctx;
	type = str;

	/* Backward compatibility */
        ret = dict_get_str (options, "transport-type", &type);
	if (ret < 0) {
		ret = dict_set_str (options, "transport-type", "socket");
		if (ret < 0)
			gf_log ("dict", GF_LOG_DEBUG,
				"setting transport-type failed");
                else
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

        /* client-bind-insecure is for clients protocol, and
         * bind-insecure for glusterd. Both mutually exclusive
        */
        ret = dict_get_str (options, "client-bind-insecure", &type);
        if (ret)
                ret = dict_get_str (options, "bind-insecure", &type);
        if (ret == 0) {
                ret = gf_string2boolean (type, &bind_insecure);
                if (ret < 0) {
                        gf_log ("rcp-transport", GF_LOG_WARNING,
                                "bind-insecure option %s is not a"
                                " valid bool option", type);
                        goto fail;
                }
                if (_gf_true == bind_insecure)
                        trans->bind_insecure = 1;
                else
                        trans->bind_insecure = 0;
        } else {
                trans->bind_insecure = 0;
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

        if (this->options)
                dict_unref (this->options);
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
                        this->notify (this, this->mydata, RPC_TRANSPORT_CLEANUP,
                                      NULL);
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
        GF_VALIDATE_OR_GOTO ("rpc", this, out);

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
        int32_t ret = -1;
        GF_VALIDATE_OR_GOTO ("rpc", trans, out);

        trans->notify = notify;
        trans->mydata = mydata;

        ret = 0;
out:
        return ret;
}

//give negative values to skip setting that value
//this function asserts if both the values are negative.
//why call it if you dont set it.
int
rpc_transport_keepalive_options_set (dict_t *options, int32_t interval,
                                     int32_t time)
{
        int                     ret = -1;

        GF_ASSERT (options);
        GF_ASSERT ((interval > 0) || (time > 0));

        ret = dict_set_int32 (options,
                "transport.socket.keepalive-interval", interval);
        if (ret)
                goto out;

        ret = dict_set_int32 (options,
                "transport.socket.keepalive-time", time);
        if (ret)
                goto out;
out:
        return ret;
}

int
rpc_transport_inet_options_build (dict_t **options, const char *hostname,
                                  int port)
{
        dict_t          *dict = NULL;
        char            *host = NULL;
        int             ret = -1;

        GF_ASSERT (options);
        GF_ASSERT (hostname);
        GF_ASSERT (port >= 1024);

        dict = dict_new ();
        if (!dict)
                goto out;

        host = gf_strdup ((char*)hostname);
        if (!hostname) {
                ret = -1;
                goto out;
        }

        ret = dict_set_dynstr (dict, "remote-host", host);
        if (ret) {
                gf_log (THIS->name, GF_LOG_WARNING,
                        "failed to set remote-host with %s", host);
                goto out;
        }

        ret = dict_set_int32 (dict, "remote-port", port);
        if (ret) {
                gf_log (THIS->name, GF_LOG_WARNING,
                        "failed to set remote-port with %d", port);
                goto out;
        }
        ret = dict_set_str (dict, "transport.address-family", "inet/inet6");
        if (ret) {
                gf_log (THIS->name, GF_LOG_WARNING,
                        "failed to set addr-family with inet");
                goto out;
        }

        ret = dict_set_str (dict, "transport-type", "socket");
        if (ret) {
                gf_log (THIS->name, GF_LOG_WARNING,
                        "failed to set trans-type with socket");
                goto out;
        }

        *options = dict;
out:
        if (ret) {
                if (host)
                        GF_FREE (host);
                if (dict)
                        dict_unref (dict);
        }

        return ret;
}
