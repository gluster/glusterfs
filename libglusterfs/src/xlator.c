/*
  Copyright (c) 2006-2010 Gluster, Inc. <http://www.gluster.com>
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

#include "xlator.h"
#include <dlfcn.h>
#include <netdb.h>
#include <fnmatch.h>
#include "defaults.h"

#define SET_DEFAULT_FOP(fn) do {			\
		if (!xl->fops->fn)			\
			xl->fops->fn = default_##fn;	\
	} while (0)

#define SET_DEFAULT_CBK(fn) do {			\
		if (!xl->cbks->fn)			\
			xl->cbks->fn = default_##fn;	\
	} while (0)


#define GF_OPTION_LIST_EMPTY(_opt) (_opt->value[0] == NULL)

static void
fill_defaults (xlator_t *xl)
{
	if (xl == NULL)	{
		gf_log ("xlator", GF_LOG_DEBUG, "invalid argument");
		return;
	}

	SET_DEFAULT_FOP (create);
	SET_DEFAULT_FOP (open);
	SET_DEFAULT_FOP (stat);
	SET_DEFAULT_FOP (readlink);
	SET_DEFAULT_FOP (mknod);
	SET_DEFAULT_FOP (mkdir);
	SET_DEFAULT_FOP (unlink);
	SET_DEFAULT_FOP (rmdir);
	SET_DEFAULT_FOP (symlink);
	SET_DEFAULT_FOP (rename);
	SET_DEFAULT_FOP (link);
	SET_DEFAULT_FOP (truncate);
	SET_DEFAULT_FOP (readv);
	SET_DEFAULT_FOP (writev);
	SET_DEFAULT_FOP (statfs);
	SET_DEFAULT_FOP (flush);
	SET_DEFAULT_FOP (fsync);
	SET_DEFAULT_FOP (setxattr);
	SET_DEFAULT_FOP (getxattr);
	SET_DEFAULT_FOP (fsetxattr);
	SET_DEFAULT_FOP (fgetxattr);
	SET_DEFAULT_FOP (removexattr);
	SET_DEFAULT_FOP (opendir);
	SET_DEFAULT_FOP (readdir);
	SET_DEFAULT_FOP (readdirp);
	SET_DEFAULT_FOP (fsyncdir);
	SET_DEFAULT_FOP (access);
	SET_DEFAULT_FOP (ftruncate);
	SET_DEFAULT_FOP (fstat);
	SET_DEFAULT_FOP (lk);
	SET_DEFAULT_FOP (inodelk);
	SET_DEFAULT_FOP (finodelk);
	SET_DEFAULT_FOP (entrylk);
	SET_DEFAULT_FOP (fentrylk);
	SET_DEFAULT_FOP (lookup);
	SET_DEFAULT_FOP (rchecksum);
	SET_DEFAULT_FOP (xattrop);
	SET_DEFAULT_FOP (fxattrop);
        SET_DEFAULT_FOP (setattr);
        SET_DEFAULT_FOP (fsetattr);

        SET_DEFAULT_FOP (getspec);

	SET_DEFAULT_CBK (release);
	SET_DEFAULT_CBK (releasedir);
	SET_DEFAULT_CBK (forget);

	if (!xl->notify)
		xl->notify = default_notify;

        if (!xl->mem_acct_init)
                xl->mem_acct_init = default_mem_acct_init;

	return;
}

int
_volume_option_value_validate (xlator_t *xl,
			       data_pair_t *pair,
			       volume_option_t *opt)
{
	int       i = 0;
	int       ret = -1;
 	uint64_t  input_size = 0;
	long long inputll = 0;

	/* Key is valid, validate the option */
	switch (opt->type) {
	case GF_OPTION_TYPE_PATH:
	{
                if (strstr (pair->value->data, "../")) {
                        gf_log (xl->name, GF_LOG_ERROR,
                                "invalid path given '%s'",
                                pair->value->data);
                        ret = -1;
                        goto out;
                }

                /* Make sure the given path is valid */
		if (pair->value->data[0] != '/') {
			gf_log (xl->name, GF_LOG_WARNING,
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
			gf_log (xl->name, GF_LOG_ERROR,
				"invalid number format \"%s\" in "
				"\"option %s\"",
				pair->value->data, pair->key);
			goto out;
		}

		if ((opt->min == 0) && (opt->max == 0)) {
			gf_log (xl->name, GF_LOG_DEBUG,
				"no range check required for "
				"'option %s %s'",
				pair->key, pair->value->data);
			ret = 0;
			break;
		}
		if ((inputll < opt->min) ||
		    (inputll > opt->max)) {
			gf_log (xl->name, GF_LOG_WARNING,
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
			gf_log (xl->name, GF_LOG_ERROR,
				"invalid size format \"%s\" in "
				"\"option %s\"",
				pair->value->data, pair->key);
			goto out;
		}

		if ((opt->min == 0) && (opt->max == 0)) {
			gf_log (xl->name, GF_LOG_DEBUG,
				"no range check required for "
				"'option %s %s'",
				pair->key, pair->value->data);
			ret = 0;
			break;
		}
		if ((input_size < opt->min) ||
		    (input_size > opt->max)) {
			gf_log (xl->name, GF_LOG_WARNING,
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
			gf_log (xl->name, GF_LOG_ERROR,
				"option %s %s: '%s' is not a valid "
				"boolean value",
				pair->key, pair->value->data,
				pair->value->data);
			goto out;
		}
		ret = 0;
	}
	break;
	case GF_OPTION_TYPE_XLATOR:
	{
		/* Check if the value is one of the xlators */
		xlator_t *xlopt = xl;
		while (xlopt->prev)
			xlopt = xlopt->prev;

		while (xlopt) {
			if (strcmp (pair->value->data,
				    xlopt->name) == 0) {
				ret = 0;
				break;
			}
			xlopt = xlopt->next;
		}
		if (!xlopt) {
			gf_log (xl->name, GF_LOG_ERROR,
				"option %s %s: '%s' is not a "
				"valid volume name",
				pair->key, pair->value->data,
				pair->value->data);
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
 #ifdef  GF_DARWIN_HOST_OS
                        if (fnmatch (opt->value[i],
                                     pair->value->data, 0) == 0) {
 #else
                        if (fnmatch (opt->value[i],
                                     pair->value->data, FNM_EXTMATCH) == 0) {
 #endif
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

			gf_log (xl->name, GF_LOG_ERROR,
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
			gf_log (xl->name, GF_LOG_ERROR,
				"invalid percent format \"%s\" "
				"in \"option %s\"",
				pair->value->data, pair->key);
			goto out;
		}

		if ((percent < 0) || (percent > 100)) {
			gf_log (xl->name, GF_LOG_ERROR,
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
				gf_log (xl->name, GF_LOG_DEBUG,
					"value given was greater than 100, "
					"assuming this is actually a size");
        		        if (gf_string2bytesize (pair->value->data,
      	        			                &input_size) == 0) {
			       	        /* Check the range */
                                if ((opt->min == 0) && (opt->max == 0)) {
	       	        		        gf_log (xl->name, GF_LOG_DEBUG,
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
        	       			        gf_log (xl->name, GF_LOG_ERROR,
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
					gf_log (xl->name, GF_LOG_ERROR,
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
       	        		        gf_log (xl->name, GF_LOG_DEBUG,
       	      	        		        "no range check required for "
               	      		      		"'option %s %s'",
               	        	        	pair->key, pair->value->data);
					// It is a size
		                        ret = 0;
      				        goto out;
             			}
	        	        if ((input_size < opt->min) ||
      			            (input_size > opt->max)) {
       	       				gf_log (xl->name, GF_LOG_ERROR,
               	      			        "'%"PRId64"' in 'option %s %s'"
                                                " is out of range [%"PRId64" -"
                                                " %"PRId64"]",
              	        	        	input_size, pair->key,
       		                        	pair->value->data,
       		        	                opt->min, opt->max);
				}
			} else {
				// It's not a percent or size
				gf_log (xl->name, GF_LOG_ERROR,
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
			gf_log (xl->name,
				GF_LOG_ERROR,
				"invalid time format \"%s\" in "
				"\"option %s\"",
				pair->value->data, pair->key);
			goto out;
		}

		if ((opt->min == 0) && (opt->max == 0)) {
			gf_log (xl->name, GF_LOG_DEBUG,
				"no range check required for "
				"'option %s %s'",
				pair->key, pair->value->data);
			ret = 0;
			goto out;
		}
		if ((input_time < opt->min) ||
		    (input_time > opt->max)) {
			gf_log (xl->name, GF_LOG_ERROR,
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
			gf_log (xl->name,
				GF_LOG_ERROR,
				"invalid time format \"%s\" in \"option %s\"",
				pair->value->data, pair->key);
			goto out;
		}

		if (input_time < 0.0) {
			gf_log (xl->name,
				GF_LOG_ERROR,
				"invalid time format \"%s\" in \"option %s\"",
				pair->value->data, pair->key);
			goto out;
		}

		if ((opt->min == 0) && (opt->max == 0)) {
			gf_log (xl->name, GF_LOG_DEBUG,
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
                } else {
			gf_log (xl->name, GF_LOG_ERROR, "internet address '%s'"
				" does not conform to standards.",
				pair->value->data);
			goto out;
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

int
validate_xlator_volume_options (xlator_t *xl, volume_option_t *opt)
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
 	pairs = xl->options->members_list;
 	while (pairs) {
		ret = -1;
        for (index = 0; opt[index].key && opt[index].key[0] ; index++) {
  			trav = &(opt[index]);
			for (i = 0 ; (i < ZR_VOLUME_MAX_NUM_KEY) && trav->key[i]; i++) {
				/* Check if the key is valid */
				if (fnmatch (trav->key[i],
					     pairs->key, FNM_NOESCAPE) == 0) {
					ret = 0;
					break;
				}
			}
			if (!ret) {
				if (i) {
					gf_log (xl->name, GF_LOG_WARNING,
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
			ret = _volume_option_value_validate (xl, pairs, trav);
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
xlator_set_type_virtual (xlator_t *xl, const char *type)
{
	if (xl == NULL || type == NULL)	{
		gf_log ("xlator", GF_LOG_DEBUG, "invalid argument");
		return -1;
	}

        xl->type = gf_strdup (type);

        if (xl->type)
                return 0;
        else
                return -1;
}


int32_t
xlator_dynload (xlator_t *xl)
{
        int   ret = 0;
	char *name = NULL;
	void *handle = NULL;
	volume_opt_list_t *vol_opt = NULL;

	ret = gf_asprintf (&name, "%s/%s.so", XLATORDIR, xl->type);
        if (-1 == ret) {
                gf_log ("xlator", GF_LOG_ERROR, "asprintf failed");
                return -1;
        }

	gf_log ("xlator", GF_LOG_TRACE, "attempt to load file %s", name);

	handle = dlopen (name, RTLD_NOW|RTLD_GLOBAL);
	if (!handle) {
		gf_log ("xlator", GF_LOG_DEBUG, "%s", dlerror ());
                GF_FREE (name);
		return -1;
	}
        xl->dlhandle = handle;

	if (!(xl->fops = dlsym (handle, "fops"))) {
		gf_log ("xlator", GF_LOG_DEBUG, "dlsym(fops) on %s",
			dlerror ());
                GF_FREE (name);
		return -1;
	}

	if (!(xl->cbks = dlsym (handle, "cbks"))) {
		gf_log ("xlator", GF_LOG_DEBUG, "dlsym(cbks) on %s",
			dlerror ());
                GF_FREE (name);
		return -1;
	}

	if (!(xl->init = dlsym (handle, "init"))) {
		gf_log ("xlator", GF_LOG_DEBUG, "dlsym(init) on %s",
			dlerror ());
                GF_FREE (name);
		return -1;
	}

	if (!(xl->fini = dlsym (handle, "fini"))) {
		gf_log ("xlator", GF_LOG_DEBUG, "dlsym(fini) on %s",
			dlerror ());
                GF_FREE (name);
		return -1;
	}

	if (!(xl->notify = dlsym (handle, "notify"))) {
		gf_log ("xlator", GF_LOG_DEBUG,
			"dlsym(notify) on %s -- neglecting", dlerror ());
	}

	if (!(xl->dumpops = dlsym (handle, "dumpops"))) {
		gf_log ("xlator", GF_LOG_DEBUG,
			"dlsym(dumpops) on %s -- neglecting", dlerror ());
	}

        if (!(xl->mem_acct_init = dlsym (handle, "mem_acct_init"))) {
                gf_log (xl->name, GF_LOG_DEBUG,
                        "dlsym(mem_acct_init) on %s -- neglecting",
                        dlerror ());
        }

	if (!(xl->reconfigure = dlsym (handle, "reconfigure"))) {
		gf_log ("xlator", GF_LOG_DEBUG,
			"dlsym(reconfigure) on %s -- neglecting",
			dlerror());
	}

        if (!(xl->validate_options = dlsym (handle, "validate_options"))) {
                gf_log ("xlator", GF_LOG_DEBUG,
                        "dlsym(validate_options) on %s -- neglecting",
                        dlerror());
        }


	INIT_LIST_HEAD (&xl->volume_options);

	vol_opt = GF_CALLOC (1, sizeof (volume_opt_list_t),
                         gf_common_mt_volume_opt_list_t);

        if (!vol_opt) {
                GF_FREE (name);
                return -1;
        }

	if (!(vol_opt->given_opt = dlsym (handle, "options"))) {
		dlerror ();
		gf_log (xl->name, GF_LOG_DEBUG,
			"Strict option validation not enforced -- neglecting");
	}
	list_add_tail (&vol_opt->list, &xl->volume_options);

	fill_defaults (xl);

	GF_FREE (name);
	return 0;
}


int32_t
xlator_set_type (xlator_t *xl, const char *type)
{
        int ret = 0;

        ret = xlator_set_type_virtual (xl, type);
        if (!ret)
                ret = xlator_dynload (xl);

        return ret;
}


void
xlator_foreach (xlator_t *this,
		void (*fn)(xlator_t *each,
			   void *data),
		void *data)
{
	xlator_t *first = NULL;

	if (this == NULL || fn == NULL || data == NULL)	{
		gf_log ("xlator", GF_LOG_DEBUG, "invalid argument");
		return;
	}

	first = this;

	while (first->prev)
		first = first->prev;

	while (first) {
		fn (first, data);
		first = first->next;
	}
}


xlator_t *
xlator_search_by_name (xlator_t *any, const char *name)
{
	xlator_t *search = NULL;

	if (any == NULL || name == NULL) {
		gf_log ("xlator", GF_LOG_DEBUG, "invalid argument");
		return NULL;
	}

	search = any;

	while (search->prev)
		search = search->prev;

	while (search) {
		if (!strcmp (search->name, name))
			break;
		search = search->next;
	}

	return search;
}


static int
__xlator_init(xlator_t *xl)
{
        xlator_t *old_THIS = NULL;
        int       ret = 0;

        old_THIS = THIS;
        THIS = xl;

        ret = xl->init (xl);

        THIS = old_THIS;

        return ret;
}


int
xlator_init (xlator_t *xl)
{
	int32_t ret = 0;

	if (xl == NULL)	{
		gf_log ("xlator", GF_LOG_DEBUG, "invalid argument");
		return 0;
	}

        ret = -1;

        if (xl->mem_acct_init)
                xl->mem_acct_init (xl);

        if (!xl->init) {
                gf_log (xl->name, GF_LOG_DEBUG, "No init() found");
                goto out;
        }

        ret = __xlator_init (xl);

        if (ret) {
                gf_log (xl->name, GF_LOG_ERROR,
                        "Initialization of volume '%s' failed,"
                        " review your volfile again",
                        xl->name);
                goto out;
        }

        xl->init_succeeded = 1;

        ret = 0;
out:
	return ret;
}


static void
xlator_fini_rec (xlator_t *xl)
{
	xlator_list_t *trav = NULL;

	if (xl == NULL)	{
		gf_log ("xlator", GF_LOG_DEBUG, "invalid argument");
		return;
	}

	trav = xl->children;

	while (trav) {
		if (!trav->xlator->init_succeeded) {
			break;
		}

		xlator_fini_rec (trav->xlator);
		gf_log (trav->xlator->name, GF_LOG_DEBUG, "fini done");
		trav = trav->next;
	}

	if (xl->init_succeeded) {
		if (xl->fini) {
			xl->fini (xl);
		} else {
			gf_log (xl->name, GF_LOG_DEBUG, "No fini() found");
		}
		xl->init_succeeded = 0;
	}
}

static int
xlator_reconfigure_rec (xlator_t *old_xl, xlator_t *new_xl)
{
	xlator_list_t *trav1 = NULL;
        xlator_list_t *trav2 = NULL;
        int32_t       ret    = 0;

	if (old_xl == NULL || new_xl == NULL)	{
		gf_log ("xlator", GF_LOG_DEBUG, "invalid argument");
		return -1;
	}

	trav1 = old_xl->children;
        trav2 = new_xl->children;

	while (trav1 && trav2) {
		ret = xlator_reconfigure_rec (trav1->xlator, trav2->xlator);

                if (ret)
                        goto out;

		gf_log (trav1->xlator->name, GF_LOG_DEBUG, "reconfigured");

		trav1 = trav1->next;
                trav2 = trav2->next;
	}

        if (old_xl->reconfigure) {
                ret = old_xl->reconfigure (old_xl, new_xl->options);
                if (ret)
                        goto out;
        }
        else
                gf_log (old_xl->name, GF_LOG_DEBUG, "No reconfigure() found");

out:
        return ret;
}

int
xlator_validate_rec (xlator_t *xlator, char **op_errstr)
{
        xlator_list_t *trav = NULL;

        if (xlator == NULL )    {
                gf_log ("xlator", GF_LOG_DEBUG, "invalid argument");
                return -1;
        }

        trav = xlator->children;

        while (trav) {
                if (xlator_validate_rec (trav->xlator, op_errstr) )
                        return -1;

                trav = trav->next;
        }

        if (xlator_dynload (xlator))
                gf_log ("", GF_LOG_DEBUG, "Did not load the symbols");

        if (xlator->validate_options) {
                if (xlator->validate_options (xlator, xlator->options,
                    op_errstr)) {
                        gf_log ("", GF_LOG_DEBUG, "%s", *op_errstr);
                        return -1;
                }
                gf_log (xlator->name, GF_LOG_DEBUG, "Validated option");

        }

        gf_log (xlator->name, GF_LOG_DEBUG, "No validate_options() found");

        return 0;
}

int
graph_reconf_validateopt (glusterfs_graph_t *graph,
                          char **op_errstr)
{
        xlator_t *xlator = NULL;
        int ret = -1;

        GF_ASSERT (graph);

        xlator = graph->first;

        ret = xlator_validate_rec (xlator, op_errstr);

        return ret;
}
int
xlator_notify (xlator_t *xl, int event, void *data, ...)
{
        xlator_t *old_THIS = NULL;
        int       ret = 0;

        old_THIS = THIS;
        THIS = xl;

        ret = xl->notify (xl, event, data);

        THIS = old_THIS;

        return ret;
}


int
xlator_mem_acct_init (xlator_t *xl, int num_types)
{
        int             i = 0;
        int             ret = 0;

        if (!gf_mem_acct_is_enabled())
                return 0;

        if (!xl)
                return -1;

        xl->mem_acct.num_types = num_types;

        xl->mem_acct.rec = calloc(num_types, sizeof(struct mem_acct_rec));

        if (!xl->mem_acct.rec) {
                gf_log("xlator", GF_LOG_ERROR, "Out of Memory");
                return -1;
        }

        for (i = 0; i < num_types; i++) {
                ret = LOCK_INIT(&(xl->mem_acct.rec[i].lock));
                if (ret) {
                        fprintf(stderr, "Unable to lock..errno : %d",errno);
                }
        }

        gf_log(xl->name, GF_LOG_DEBUG, "Allocated mem_acct_rec for %d types",
                        num_types);

        return 0;
}

void
xlator_tree_fini (xlator_t *xl)
{
	xlator_t *top = NULL;

	if (xl == NULL)	{
		gf_log ("xlator", GF_LOG_DEBUG, "invalid argument");
		return;
	}

	top = xl;
	xlator_fini_rec (top);
}

int
xlator_tree_reconfigure (xlator_t *old_xl, xlator_t *new_xl)
{
        xlator_t *new_top = NULL;
        xlator_t *old_top = NULL;

        GF_ASSERT (old_xl);
        GF_ASSERT (new_xl);

        old_top = old_xl;
        new_top = new_xl;

	return xlator_reconfigure_rec (old_top, new_top);
}


int
xlator_tree_free (xlator_t *tree)
{
  xlator_t *trav = tree, *prev = tree;

  if (!tree) {
    gf_log ("parser", GF_LOG_ERROR, "Translator tree not found");
    return -1;
  }

  while (prev) {
    trav = prev->next;
    dict_destroy (prev->options);
    GF_FREE (prev->name);
    GF_FREE (prev->type);
    GF_FREE (prev);
    prev = trav;
  }

  return 0;
}


void
loc_wipe (loc_t *loc)
{
        if (loc->inode) {
                inode_unref (loc->inode);
                loc->inode = NULL;
        }
        if (loc->path) {
                GF_FREE ((char *)loc->path);
                loc->path = NULL;
        }

        if (loc->parent) {
                inode_unref (loc->parent);
                loc->parent = NULL;
        }
}


int
loc_copy (loc_t *dst, loc_t *src)
{
	int ret = -1;

	dst->ino = src->ino;

	if (src->inode)
		dst->inode = inode_ref (src->inode);

	if (src->parent)
		dst->parent = inode_ref (src->parent);

        dst->path = gf_strdup (src->path);

	if (!dst->path)
		goto out;

	dst->name = strrchr (dst->path, '/');
	if (dst->name)
		dst->name++;

	ret = 0;
out:
        if (ret == -1) {
                if (dst->inode)
                        inode_unref (dst->inode);

                if (dst->parent)
                        inode_unref (dst->parent);
        }
	return ret;
}


int
xlator_list_destroy (xlator_list_t *list)
{
        xlator_list_t *next = NULL;

        while (list) {
                next = list->next;
                GF_FREE (list);
                list = next;
        }

        return 0;
}


int
xlator_destroy (xlator_t *xl)
{
        volume_opt_list_t *vol_opt = NULL;
        volume_opt_list_t *tmp     = NULL;

        if (!xl)
                return 0;

        if (xl->name)
                GF_FREE (xl->name);
        if (xl->type)
                GF_FREE (xl->type);
        if (xl->dlhandle)
                dlclose (xl->dlhandle);
        if (xl->options)
                dict_destroy (xl->options);

        xlator_list_destroy (xl->children);

        xlator_list_destroy (xl->parents);

        list_for_each_entry_safe (vol_opt, tmp, &xl->volume_options, list) {
                list_del_init (&vol_opt->list);
                GF_FREE (vol_opt);
        }

        GF_FREE (xl);

        return 0;
}
int
is_gf_log_command (xlator_t *this, const char *name, char *value)
{
        xlator_t       *trav        = NULL;
        char            key[1024]   = {0,};
        int             ret         = -1;
        int             log_level   = -1;
        gf_boolean_t    syslog_flag = 0;
        glusterfs_ctx_t *ctx        = NULL;

        if (!strcmp ("trusted.glusterfs.syslog", name)) {
                ret = gf_string2boolean (value, &syslog_flag);
                if (ret) {
                        ret = EOPNOTSUPP;
                        goto out;
                }
                if (syslog_flag)
                        gf_log_enable_syslog ();
                else
                        gf_log_disable_syslog ();

                goto out;
        }

        if (fnmatch ("trusted.glusterfs*set-log-level", name, FNM_NOESCAPE))
                goto out;

        log_level = glusterd_check_log_level (value);
        if (log_level == -1) {
                ret = EOPNOTSUPP;
                goto out;
        }

        /* Some crude way to change the log-level of process */
        if (!strcmp (name, "trusted.glusterfs.set-log-level")) {
                /* */
                gf_log ("glusterfs", gf_log_get_loglevel(),
                        "setting log level to %d (old-value=%d)",
                        log_level, gf_log_get_loglevel());
                gf_log_set_loglevel (log_level);
                ret = 0;
                goto out;
        }
        if (!strcmp (name, "trusted.glusterfs.fuse.set-log-level")) {
                /* */
                gf_log (this->name, gf_log_get_xl_loglevel (this),
                        "setting log level to %d (old-value=%d)",
                        log_level, gf_log_get_xl_loglevel (this));
                gf_log_set_xl_loglevel (this, log_level);
                ret = 0;
                goto out;
        }

        ctx = glusterfs_ctx_get();
        if (!ctx)
                goto out;
        if (!ctx->active)
                goto out;
        trav = ctx->active->top;

        while (trav) {
                snprintf (key, 1024, "trusted.glusterfs.%s.set-log-level",
                          trav->name);
                if (fnmatch (name, key, FNM_NOESCAPE) == 0) {
                        gf_log (trav->name, gf_log_get_xl_loglevel (trav),
                                "setting log level to %d (old-value=%d)",
                                log_level, gf_log_get_xl_loglevel (trav));
                        gf_log_set_xl_loglevel (trav, log_level);
                        ret = 0;
                }
                trav = trav->next;
        }
out:
        return ret;
}

int
glusterd_check_log_level (const char *value)
{
        int log_level = -1;

        if (!strcasecmp (value, "CRITICAL")) {
                log_level = GF_LOG_CRITICAL;
        } else if (!strcasecmp (value, "ERROR")) {
                log_level = GF_LOG_ERROR;
        } else if (!strcasecmp (value, "WARNING")) {
                log_level = GF_LOG_WARNING;
        } else if (!strcasecmp (value, "INFO")) {
                log_level = GF_LOG_INFO;
        } else if (!strcasecmp (value, "DEBUG")) {
                log_level = GF_LOG_DEBUG;
        } else if (!strcasecmp (value, "TRACE")) {
                log_level = GF_LOG_TRACE;
        } else if (!strcasecmp (value, "NONE")) {
                log_level = GF_LOG_NONE;
        }

        if (log_level == -1)
                gf_log ("", GF_LOG_ERROR, "Invalid log-level. possible values "
                        "are DEBUG|WARNING|ERROR|CRITICAL|NONE|TRACE");

        return log_level;
}
