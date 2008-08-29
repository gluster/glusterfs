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

#define SET_DEFAULT_FOP(fn) do {    \
    if (!xl->fops->fn)              \
       xl->fops->fn = default_##fn; \
} while (0)

#define SET_DEFAULT_MOP(fn) do {        \
    if (!xl->mops->fn)                  \
       xl->mops->fn = default_##fn;     \
} while (0)

static void 
fill_defaults (xlator_t *xl)
{
	if (xl == NULL) {
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
	SET_DEFAULT_FOP (rmelem);
	SET_DEFAULT_FOP (incver);
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
	SET_DEFAULT_FOP (close);
	SET_DEFAULT_FOP (fsync);
	SET_DEFAULT_FOP (setxattr);
	SET_DEFAULT_FOP (getxattr);
	SET_DEFAULT_FOP (removexattr);
	SET_DEFAULT_FOP (opendir);
	SET_DEFAULT_FOP (readdir);
	SET_DEFAULT_FOP (closedir);
	SET_DEFAULT_FOP (fsyncdir);
	SET_DEFAULT_FOP (access);
	SET_DEFAULT_FOP (ftruncate);
	SET_DEFAULT_FOP (fstat);
	SET_DEFAULT_FOP (lk);
	SET_DEFAULT_FOP (gf_lk);
	SET_DEFAULT_FOP (lookup);
	SET_DEFAULT_FOP (forget);
	SET_DEFAULT_FOP (fchown);
	SET_DEFAULT_FOP (fchmod);
	SET_DEFAULT_FOP (setdents);
	SET_DEFAULT_FOP (getdents);
	SET_DEFAULT_FOP (checksum);
	SET_DEFAULT_FOP (xattrop);

	SET_DEFAULT_MOP (stats);
	SET_DEFAULT_MOP (lock);
	SET_DEFAULT_MOP (unlock);
	SET_DEFAULT_MOP (listlocks);

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
	int8_t           input8  = 0;
	int32_t          input32 = 0;
	int64_t          input64 = 0;
	int64_t          input_size = 0;

	if (!xl->std_options)
		return 0;

	/* First search for not supported options, if any report error */
	pairs = xl->options->members_list;
	while (pairs) {
		valid = 0;
		for (index = 0; xl->std_options[index].key ; index++) {
			trav = &(xl->std_options[index]);
			if (trav->strict_match) {
				if (strcmp (trav->key, pairs->key) == 0) {
					valid = 1;
					break;
				}
			} else {
				if (strncmp (trav->key, pairs->key, strlen (trav->key)) == 0) {
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
				if (!xl->std_options[index].strict_match)
					strcat (allowed_options, "....");
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
		case GF_OPTION_TYPE_INT8:
			/* Check the range */
			if (gf_string2int8 (pairs->value->data, &input8) != 0) {
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
			if ((input8 <= (int8_t)trav->min_value) || (input8 >= (int8_t)trav->max_value)) {
				gf_log (xl->name, GF_LOG_ERROR,
					"'%d' in 'option %s %s' is out of range [%d - %d]",
					input8, pairs->key, pairs->value->data, 
					(int8_t)trav->min_value, (int8_t)trav->max_value);
				return -1;
			}		     
			break;

		case GF_OPTION_TYPE_INT32:
			/* Check the range */
			if (gf_string2int32 (pairs->value->data, &input32) != 0) {
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
			if ((input32 <= (int32_t)trav->min_value) || (input32 >= (int32_t)trav->max_value)) {
				gf_log (xl->name, GF_LOG_ERROR,
					"'%d' in 'option %s %s' is out of range [%d - %d]",
					input32, pairs->key, pairs->value->data, 
					(int32_t)trav->min_value, (int32_t)trav->max_value);
				return -1;
			}		     
			break;

		case GF_OPTION_TYPE_INT64:
			/* Check the range */
			if (gf_string2longlong (pairs->value->data, &input64) != 0) {
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
			if ((input64 <= trav->min_value) || (input64 >= trav->max_value)) {
				gf_log (xl->name, GF_LOG_ERROR,
					"'%"PRId64"' in 'option %s %s' is out of range [%"PRId64" - %"PRId64"]",
					input64, pairs->key, pairs->value->data, 
					trav->min_value, trav->max_value);
				return -1;
			}		     
			break;

		case GF_OPTION_TYPE_SIZET:
		{
			size_t tmp_size = 0;

			/* Check the range */
			if (gf_string2bytesize (pairs->value->data, &tmp_size) != 0) {
				gf_log (xl->name, 
					GF_LOG_ERROR, 
					"invalid number format \"%s\" in \"option %s\"", 
					pairs->value->data, pairs->key);
				return -1;
			}

			input_size = (int64_t)tmp_size;
			if (trav->min_value == -1) {
				gf_log (xl->name, GF_LOG_DEBUG, 
					"no range check required for 'option %s %s'",
					pairs->key, pairs->value->data);
				break;
			}
			if ((input_size <= trav->min_value ) || (input_size >= trav->max_value)) {
				gf_log (xl->name, GF_LOG_ERROR,
					"'%"PRId64"' in 'option %s %s' is out of range [%"PRId64" - %"PRId64"]", 
					input_size, pairs->key, pairs->value->data,
					trav->min_value, trav->max_value); 
				return -1;
			}
			break;
		}
		case GF_OPTION_TYPE_ANY:
		case GF_OPTION_TYPE_STR:
		default:
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

	if ((xl == NULL) || (type == NULL)) {
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
		gf_log (xl->name, GF_LOG_ERROR, "dlsym(fops) on %s", dlerror ());
		return -1;
	}
	if (!(xl->mops = dlsym (handle, "mops"))) {
		gf_log (xl->name, GF_LOG_ERROR, "dlsym(mops) on %s", dlerror ());
		return -1;
	}

	if (!(xl->init = dlsym (handle, "init"))) {
		gf_log (xl->name, GF_LOG_ERROR, "dlsym(init) on %s", dlerror ());
		return -1;
	}

	if (!(xl->fini = dlsym (handle, "fini"))) {
		gf_log (xl->name, GF_LOG_ERROR, "dlsym(fini) on %s", dlerror ());
		return -1;
	}

	if (!(xl->notify = dlsym (handle, "notify"))) {
		gf_log (xl->name, GF_LOG_DEBUG, 
			"dlsym(notify) on %s -- neglecting", dlerror ());
	}

	if (!(xl->std_options = dlsym (handle, "options"))) {
		dlerror ();
		gf_log (xl->name, GF_LOG_WARNING, 
			"strict option validation is not enforced -- neglecting");
	}

	fill_defaults (xl);

	FREE (name);
	return 0;
}

static void
_foreach_dfs (xlator_t *this,
	      void (*fn)(xlator_t *each,
			 void *data),
	      void *data)
{
	xlator_list_t *child = NULL;
  
	if ((this == NULL) || (fn == NULL) || (data == NULL)) {
		gf_log ("xlator", GF_LOG_ERROR, "invalid argument");
		return;
	}
  
	child = this->children;
  
	while (child) {
		_foreach_dfs (child->xlator, fn, data);
		child = child->next;
	}

	fn (this, data); 
}

void
xlator_foreach (xlator_t *this,
		void (*fn)(xlator_t *each,
			   void *data),
		void *data)
{
	xlator_t *first = NULL;
  
	if ((this == NULL) || (fn == NULL) || (data == NULL)) {
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

	if ((any == NULL) || (name == NULL)) {
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
  
	if (xl == NULL) {
		gf_log ("xlator", GF_LOG_ERROR, "invalid argument");
		return 0;
	}
  
	trav = xl->children;
  
	while (trav) {
		ret = xlator_init_rec (trav->xlator);
		if (ret != 0)
			break;
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

	if (xl == NULL) {
		gf_log ("xlator", GF_LOG_ERROR, "invalid argument");
		return 0;
	}
  
	top = xl;

	while (top->parents)
		top = top->parents->xlator;

	ret = xlator_init_rec (top);

	if (ret == 0 && top->notify) {
		top->notify (top, GF_EVENT_PARENT_UP, NULL);
	}

	return ret;
}

fd_t *
fd_create (inode_t *inode)
{
	fd_t *fd = NULL;
  
	if (inode == NULL) {
		gf_log ("xlator", GF_LOG_ERROR, "invalid argument");
		return NULL;
	}
  
	fd = calloc (1, sizeof (fd_t));
	ERR_ABORT (fd);
  
	fd->ctx = get_new_dict ();
	fd->ctx->is_locked = 1;
	fd->inode = inode_ref (inode);

	LOCK (&inode->lock);
	{
		list_add (&fd->inode_list, &inode->fd_list);
	}
	UNLOCK (&inode->lock);

	return fd;
}

void
fd_destroy (fd_t *fd)
{
	if (fd == NULL) {
		gf_log ("xlator", GF_LOG_ERROR, "invalid arugument");
		return;
	}
  
	if (fd->inode == NULL) {
		gf_log ("xlator", GF_LOG_ERROR, "fd->inode is NULL");
		return;
	}
 
	LOCK (&fd->inode->lock);
	{
		list_del (&fd->inode_list);
	}
	UNLOCK (&fd->inode->lock);

	inode_unref (fd->inode);
	fd->inode = (inode_t *)0xaaaaaaaa;
	dict_destroy (fd->ctx);
	FREE (fd);
}
