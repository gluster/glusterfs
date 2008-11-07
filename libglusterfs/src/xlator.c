/*
  Copyright (c) 2006, 2007, 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "xlator.h"
#include <dlfcn.h>
#include <netdb.h>
#include "defaults.h"


#define SET_DEFAULT_FOP(fn) do {			\
		if (!xl->fops->fn)			\
			xl->fops->fn = default_##fn;	\
	} while (0)

#define SET_DEFAULT_MOP(fn) do {			\
		if (!xl->mops->fn)			\
			xl->mops->fn = default_##fn;	\
	} while (0)

#define SET_DEFAULT_CBK(fn) do {			\
		if (!xl->cbks->fn)			\
			xl->cbks->fn = default_##fn;	\
	} while (0)


static void
fill_defaults (xlator_t *xl)
{
	if (xl == NULL)	{
		gf_log ("xlator", GF_LOG_ERROR, "invalid argument");
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
	SET_DEFAULT_FOP (chmod);
	SET_DEFAULT_FOP (chown);
	SET_DEFAULT_FOP (truncate);
	SET_DEFAULT_FOP (utimens);
	SET_DEFAULT_FOP (readv);
	SET_DEFAULT_FOP (writev);
	SET_DEFAULT_FOP (statfs);
	SET_DEFAULT_FOP (flush);
	SET_DEFAULT_FOP (fsync);
	SET_DEFAULT_FOP (setxattr);
	SET_DEFAULT_FOP (getxattr);
	SET_DEFAULT_FOP (removexattr);
	SET_DEFAULT_FOP (opendir);
	SET_DEFAULT_FOP (readdir);
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
	SET_DEFAULT_FOP (fchown);
	SET_DEFAULT_FOP (fchmod);
	SET_DEFAULT_FOP (setdents);
	SET_DEFAULT_FOP (getdents);
	SET_DEFAULT_FOP (checksum);
	SET_DEFAULT_FOP (xattrop);
	SET_DEFAULT_FOP (fxattrop);

	SET_DEFAULT_MOP (stats);
	SET_DEFAULT_MOP (lock);
	SET_DEFAULT_MOP (unlock);
	SET_DEFAULT_MOP (listlocks);

	SET_DEFAULT_CBK (release);
	SET_DEFAULT_CBK (releasedir);
	SET_DEFAULT_CBK (forget);

	if (!xl->notify)
		xl->notify = default_notify;

	return;
}


int32_t
xlator_validate_given_options (xlator_t *xl)
{
 	xlator_option_t *trav    = NULL;
 	data_pair_t     *pairs   = NULL;
 	int32_t          index   = 0;
 	int32_t          valid   = 0;
 	uint64_t         input_size = 0;
	long long        inputll = 0;

 	if (!xl->std_options)
 		return 0;

 	/* First search for not supported options, if any report error */
 	pairs = xl->options->members_list;
 	while (pairs) {
  		valid = 0;
  		for (index = 0; xl->std_options[index].key ; index++) {
  			trav = &(xl->std_options[index]);
  			if (trav->num_char_to_match) {
  				if (strncmp (trav->key, pairs->key,
					     trav->num_char_to_match) == 0) {
  					valid = 1;
  					break;
  				}
  			} else {
  				if (strcmp (trav->key, pairs->key) == 0) {
  					valid = 1;
  					break;
  				}
  			}
  		}
  		if (!valid) {
  			char allowed_options[1024];
  			allowed_options[0] = ' ';
  			allowed_options[1] = 0;
  			for (index = 0; xl->std_options[index].key ; index++) {
  				if (index)
  					strcat (allowed_options, ", ");
  				strcat (allowed_options, "'");
  				strcat (allowed_options, xl->std_options[index].key);
  				strcat (allowed_options, "'");
  			}

  			gf_log (xl->name, GF_LOG_ERROR,
  				"key (%s) in 'option %s %s' is not valid, recheck",
  				pairs->key, pairs->key, pairs->value->data);
  			gf_log (xl->name, GF_LOG_ERROR,
  				"valid options for translator type '%s' are %s",
  				xl->type, allowed_options);
  			return -1;
  		}

  		/* Key is valid, check the range and other options */
  		switch (trav->type) {
  		case GF_OPTION_TYPE_PATH:
  			/* Make sure the given path is valid */
  			if (pairs->value->data[0] != '/') {
  				gf_log (xl->name, GF_LOG_ERROR,
  					"option %s %s: '%s' is not an absolute path name",
  					pairs->key, pairs->value->data, pairs->value->data);
  				return -1;
  			}
  			break;
  		case GF_OPTION_TYPE_INT:
  			/* Check the range */
  			if (gf_string2longlong (pairs->value->data, &inputll) != 0) {
  				gf_log (xl->name,
  					GF_LOG_ERROR,
  					"invalid number format \"%s\" in \"option %s\"",
  					pairs->value->data, pairs->key);
  				return -1;
  			}

  			if (trav->min_value == -1) {
  				gf_log (xl->name, GF_LOG_DEBUG,
  					"no range check required for 'option %s %s'",
  					pairs->key, pairs->value->data);
  				break;
  			}
  			if ((inputll < trav->min_value) || (inputll > trav->max_value)) {
  				gf_log (xl->name, GF_LOG_ERROR,
  					"'%lld' in 'option %s %s' is out of range [%"PRId64" - %"PRId64"]",
  					inputll, pairs->key, pairs->value->data,
  					trav->min_value, trav->max_value);
  				return -1;
  			}
  			break;
  		case GF_OPTION_TYPE_SIZET:
  		{
  			/* Check the range */
  			if (gf_string2bytesize (pairs->value->data, &input_size) != 0) {
  				gf_log (xl->name,
  					GF_LOG_ERROR,
  					"invalid number format \"%s\" in \"option %s\"",
  					pairs->value->data, pairs->key);
  				return -1;
  			}

  			if (trav->min_value == -1) {
  				gf_log (xl->name, GF_LOG_DEBUG,
  					"no range check required for 'option %s %s'",
  					pairs->key, pairs->value->data);
  				break;
  			}
  			if ((input_size < trav->min_value) || (input_size > trav->max_value)) {
  				gf_log (xl->name, GF_LOG_ERROR,
  					"'%"PRId64"' in 'option %s %s' is out of range [%"PRId64" - %"PRId64"]",
  					input_size, pairs->key, pairs->value->data,
  					trav->min_value, trav->max_value);
  				return -1;
  			}
  			break;
  		}
  		case GF_OPTION_TYPE_BOOL:
  		{
  			/* Check if the value is one of '0|1|on|off|no|yes|true|false|enable|disable' */
			gf_boolean_t bool_value;
  			if (gf_string2boolean (pairs->value->data, &bool_value) != 0) {
  				gf_log (xl->name, GF_LOG_ERROR,
  					"option %s %s: '%s' is not valid boolean value",
  					pairs->key, pairs->value->data, pairs->value->data);
  				return -1;
  			}
  			break;
  		}
  		case GF_OPTION_TYPE_XLATOR:
  		{
  			/* Check if the value is one of the xlators */
  			xlator_t *xltrav = xl;
  			while (xltrav->prev)
  				xltrav = xltrav->prev;

  			while (xltrav) {
  				if (strcmp (pairs->value->data, xltrav->name) == 0)
  					break;
  				xltrav = xltrav->next;
  			}
  			if (!xltrav) {
  				gf_log (xl->name, GF_LOG_ERROR,
  					"option %s %s: '%s' is not a valid volume name",
  					pairs->key, pairs->value->data, pairs->value->data);
  				return -1;
  			}
  			break;
  		}
  		case GF_OPTION_TYPE_STR:
  		{
  			char *tmp_str = NULL;
  			char *tmp_char = NULL;
  			char *tmp_value = NULL;

  			/* Check if the '*str' is valid */
  			if (!trav->str)
  				break;

  			tmp_str = strdup (trav->str);
  			tmp_value = strtok_r (tmp_str, "|", &tmp_char);
  			while (tmp_value) {
  				if (strcasecmp (tmp_value, pairs->value->data) == 0)
  					break;
  				tmp_value = strtok_r (NULL, "|", &tmp_char);
  			}
  			if (!tmp_value) {
  				gf_log (xl->name, GF_LOG_ERROR,
  					"option %s %s: '%s' is not valid (possible options are '%s')",
  					pairs->key, pairs->value->data, pairs->value->data, trav->str);
  				return -1;
  			}
  			break;
  		}
  		case GF_OPTION_TYPE_PERCENT:
  		{
  			uint32_t percent = 0;

  			/* Check if the value is valid percentage */
  			if (gf_string2percent (pairs->value->data, &percent) != 0) {
  				gf_log (xl->name,
  					GF_LOG_ERROR,
  					"invalid percent format \"%s\" in \"option %s\"",
  					pairs->value->data, pairs->key);
  				return -1;
  			}

  			if (trav->min_value == -1) {
  				gf_log (xl->name, GF_LOG_DEBUG,
  					"no range check required for 'option %s %s'",
  					pairs->key, pairs->value->data);
  				break;
  			}
  			if ((percent < 0) || (percent > 100)) {
  				gf_log (xl->name, GF_LOG_ERROR,
  					"'%d' in 'option %s %s' is out of range [0 - 100]",
  					percent, pairs->key, pairs->value->data);
  				return -1;
  			}
  			break;
  		}
  		case GF_OPTION_TYPE_TIME:
  		{
  			uint32_t input_time = 0;

  			/* Check if the value is valid percentage */
  			if (gf_string2time (pairs->value->data, &input_time) != 0) {
  				gf_log (xl->name,
  					GF_LOG_ERROR,
  					"invalid time format \"%s\" in \"option %s\"",
  					pairs->value->data, pairs->key);
  				return -1;
  			}

  			if (trav->min_value == -1) {
  				gf_log (xl->name, GF_LOG_DEBUG,
  					"no range check required for 'option %s %s'",
  					pairs->key, pairs->value->data);
  				break;
  			}
  			if ((input_time < trav->min_value) || (input_time > trav->max_value)) {
  				gf_log (xl->name, GF_LOG_ERROR,
  					"'%"PRIu32"' in 'option %s %s' is out of range [%"PRId64" - %"PRId64"]",
  					input_time, pairs->key, pairs->value->data,
  					trav->min_value, trav->max_value);
  				return -1;
  			}
  			break;
  		}
  		case GF_OPTION_TYPE_ANY:
  			break;
  		}

  		pairs = pairs->next;
  	}

  	return 0;
}


int32_t
xlator_set_type (xlator_t *xl,
		 const char *type)
{
	char *name = NULL;
	void *handle = NULL;

	if (xl == NULL || type == NULL)	{
		gf_log ("xlator", GF_LOG_ERROR, "invalid argument");
		return -1;
	}

	xl->type = strdup (type);

	asprintf (&name, "%s/%s.so", XLATORDIR, type);

	gf_log ("xlator", GF_LOG_DEBUG, "attempt to load file %s", name);

	handle = dlopen (name, RTLD_NOW|RTLD_GLOBAL);
	if (!handle) {
		gf_log ("xlator", GF_LOG_ERROR, "%s", dlerror ());
		return -1;
	}

	if (!(xl->fops = dlsym (handle, "fops"))) {
		gf_log ("xlator", GF_LOG_ERROR, "dlsym(fops) on %s",
			dlerror ());
		return -1;
	}

	if (!(xl->mops = dlsym (handle, "mops"))) {
		gf_log ("xlator", GF_LOG_ERROR, "dlsym(mops) on %s",
			dlerror ());
		return -1;
	}

	if (!(xl->cbks = dlsym (handle, "cbks"))) {
		gf_log ("xlator", GF_LOG_ERROR, "dlsym(cbks) on %s",
			dlerror ());
		return -1;
	}

	if (!(xl->init = dlsym (handle, "init"))) {
		gf_log ("xlator", GF_LOG_ERROR, "dlsym(init) on %s",
			dlerror ());
		return -1;
	}

	if (!(xl->fini = dlsym (handle, "fini"))) {
		gf_log ("xlator", GF_LOG_ERROR, "dlsym(fini) on %s",
			dlerror ());
		return -1;
	}

	if (!(xl->notify = dlsym (handle, "notify"))) {
		gf_log ("xlator", GF_LOG_DEBUG,
			"dlsym(notify) on %s -- neglecting", dlerror ());
	}

	if (!(xl->std_options = dlsym (handle, "options"))) {
		dlerror ();
		gf_log (xl->name, GF_LOG_DEBUG,
			"strict option validation not enforced -- neglecting");
	}

	fill_defaults (xl);

	FREE (name);
	return 0;
}


void
xlator_foreach (xlator_t *this,
		void (*fn)(xlator_t *each,
			   void *data),
		void *data)
{
	xlator_t *first = NULL;

	if (this == NULL || fn == NULL || data == NULL)	{
		gf_log ("xlator", GF_LOG_ERROR, "invalid argument");
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
		gf_log ("xlator", GF_LOG_ERROR, "invalid argument");
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


static int32_t
xlator_init_rec (xlator_t *xl)
{
	xlator_list_t *trav = NULL;
	int32_t ret = 0;

	if (xl == NULL)	{
		gf_log ("xlator", GF_LOG_ERROR, "invalid argument");
		return 0;
	}

	trav = xl->children;

	while (trav) {
		ret = 0;
		ret = xlator_init_rec (trav->xlator);
		if (ret != 0)
			break;
		gf_log (trav->xlator->name, GF_LOG_DEBUG, "Initialization done");
		trav = trav->next;
	}

	if (!ret && !xl->ready) {
		ret = -1;
		if (xl->init) {
			ret = xl->init (xl);
			if (ret) {
				gf_log ("xlator", GF_LOG_ERROR,
					"initialization of volume '%s' failed, review your volume spec file again",
					xl->name);
			}
		} else {
			gf_log (xl->name, GF_LOG_ERROR, "No init() found");
		}
		/* This 'xl' is checked */
		xl->ready = 1;
	}

	return ret;
}


int32_t
xlator_tree_init (xlator_t *xl)
{
	xlator_t *top = NULL;
	int32_t ret = 0;

	if (xl == NULL)	{
		gf_log ("xlator", GF_LOG_ERROR, "invalid argument");
		return 0;
	}

	top = xl;
/*
	while (top->parents)
		top = top->parents->xlator;
*/
	ret = xlator_init_rec (top);

	if (ret == 0 && top->notify) {
		top->notify (top, GF_EVENT_PARENT_UP, NULL);
	}

	return ret;
}


void
loc_wipe (loc_t *loc)
{
        if (loc->inode) {
                inode_unref (loc->inode);
                loc->inode = NULL;
        }
        if (loc->path) {
                FREE (loc->path);
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

	dst->path = strdup (src->path);

	if (!dst->path)
		goto out;

	dst->name = strrchr (dst->path, '/');
	if (dst->name)
		dst->name++;

	ret = 0;
out:
	return ret;
}
