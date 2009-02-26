/*
  Copyright (c) 2006-2009 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#include <sys/time.h>
#include <stdlib.h>
#include <fnmatch.h>

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "xlator.h"
#include "scheduler.h"

struct switch_sched_array {
	xlator_t *xl;
	int32_t   eligible;
	int32_t   considered;
};

/* Select one of this struct based on the path's pattern match */
struct switch_sched_struct {
	struct switch_sched_struct *next;
	struct switch_sched_array  *array;
	char                        path_pattern[256];
	int32_t                     node_index; /* Index of the node in 
						   this pattern. */
	int32_t                     num_child;  /* Total num of child nodes 
						   with this pattern. */
};

struct switch_struct {
	struct switch_sched_struct *cond;
	struct switch_sched_array  *array;
	pthread_mutex_t             switch_mutex;
	int32_t                     child_count;
};

/* This function should return child node as '*:subvolumes' is inserterd */
static xlator_t *
switch_get_matching_xl (const char *path, struct switch_sched_struct *cond)
{
	struct switch_sched_struct *trav      = cond;
	char                       *pathname  = strdup (path);
	int                         index     = 0;

	while (trav) {
		if (fnmatch (trav->path_pattern, 
			     pathname, FNM_NOESCAPE) == 0) {
			free (pathname);
			trav->node_index %= trav->num_child;
			index = (trav->node_index++) % trav->num_child;
			return trav->array[index].xl;
		}
		trav = trav->next;
	}
	free (pathname);
	return NULL;
}


static int32_t
switch_init (xlator_t *xl)
{
	int32_t index = 0;
	data_t *data = NULL;
	char *child = NULL;
	char *tmp = NULL;
	char *childs_data = NULL;
	xlator_list_t *trav_xl = xl->children;
	struct switch_struct *switch_buf = NULL;
  
	switch_buf = CALLOC (1, sizeof (struct switch_struct));
	ERR_ABORT (switch_buf);

	while (trav_xl) {
		index++;
		trav_xl = trav_xl->next;
	}
	switch_buf->child_count = index;
	switch_buf->array = CALLOC (index + 1, 
				    sizeof (struct switch_sched_struct));
	ERR_ABORT (switch_buf->array);
	trav_xl = xl->children;
	index = 0;

	while (trav_xl) {
		switch_buf->array[index].xl = trav_xl->xlator;
		switch_buf->array[index].eligible = 1;
		trav_xl = trav_xl->next;
		index++;
	}

	data = dict_get (xl->options, "scheduler.read-only-subvolumes");
	if (data) {
		childs_data = strdup (data->data);
		child = strtok_r (childs_data, ",", &tmp);
		while (child) {
			for (index = 1; 
			     index < switch_buf->child_count; index++) {
				if (strcmp (switch_buf->array[index - 1].xl->name, child) == 0) {
					gf_log ("switch", GF_LOG_DEBUG, 
						"Child '%s' is read-only", 
						child);
					memcpy (&(switch_buf->array[index-1]),
						&(switch_buf->array[switch_buf->child_count - 1]), 
						sizeof (struct switch_sched_array));
					switch_buf->child_count--;
					break;
				}
			}
			child = strtok_r (NULL, ",", &tmp);
		}
		free (childs_data);
	}

	data = dict_get (xl->options, "scheduler.local-volume-name");
	if (data) {
		/* Means, give preference to that node first */
		gf_log ("switch", GF_LOG_DEBUG, 
			"local volume defined as %s", data->data);

		/* TODO: parse it properly, have an extra index to 
		   specify that first */
	}

	/*  *jpg:child1,child2;*mpg:child3;*:child4,child5,child6 */
	data = dict_get (xl->options, "scheduler.switch.case");
	if (data) {
		char *tmp_str = NULL; 
		char *tmp_str1 = NULL;
		char *dup_str = NULL;
		char *switch_str = NULL;
		char *pattern = NULL;
		char *childs = NULL;
		struct switch_sched_struct *switch_opt = NULL;
		struct switch_sched_struct *trav = NULL;
		/* Get the pattern for considering switch case. 
		   "option block-size *avi:10MB" etc */
		switch_str = strtok_r (data->data, ";", &tmp_str);
		while (switch_str) {
			dup_str = strdup (switch_str);
			switch_opt = 
				CALLOC (1, 
					sizeof (struct switch_sched_struct));
			ERR_ABORT (switch_opt);

			/* Link it to the main structure */
			if (switch_buf->cond) {
				/* there are already few entries */
				trav = switch_buf->cond;
				while (trav->next)
					trav = trav->next;
				trav->next = switch_opt;
			} else {
				/* First entry */
				switch_buf->cond = switch_opt;
			}
			pattern = strtok_r (dup_str, ":", &tmp_str1);
			childs = strtok_r (NULL, ":", &tmp_str1);
			if (strncmp (pattern, "*", 2) == 0) {
				gf_log ("switch", GF_LOG_WARNING,
					"'*' pattern will be taken by default "
					"for all the unconfigured child nodes,"
					" hence neglecting current option");
				switch_str = strtok_r (NULL, ";", &tmp_str);
				free (dup_str);
				continue;
			}
			memcpy (switch_opt->path_pattern, 
				pattern, strlen (pattern));
			if (childs) {
				int32_t idx = 0;
				char *tmp1 = NULL;
				char *dup_childs = NULL;
				/* TODO: get the list of child nodes for 
				   the given pattern */
				dup_childs = strdup (childs);
				child = strtok_r (dup_childs, ",", &tmp);
				while (child) {
					idx++;
					child = strtok_r (NULL, ",", &tmp);
				}
				free (dup_childs);
				child = strtok_r (childs, ",", &tmp1);
				switch_opt->num_child = idx;
				switch_opt->array = 
					CALLOC (1, idx * sizeof (struct switch_sched_array));
				ERR_ABORT (switch_opt->array);
				idx = 0;
				child = strtok_r (childs, ",", &tmp);
				while (child) {
					for (index = 1; 
					     index < switch_buf->child_count; 
					     index++) {
						if (strcmp (switch_buf->array[index - 1].xl->name, 
							    child) == 0) {
							gf_log ("switch", 
								GF_LOG_DEBUG,
								"'%s' pattern will be scheduled to \"%s\"",
								switch_opt->path_pattern, child);
							/*
							  if (switch_buf->array[index-1].considered) {
							  gf_log ("switch", GF_LOG_DEBUG, 
							  "ambiguity found, exiting");
							  return -1;
							  }
							*/
							switch_opt->array[idx].xl = switch_buf->array[index-1].xl;
							switch_buf->array[index-1].considered = 1;
							idx++;
							break;
						}
					}
					child = strtok_r (NULL, ",", &tmp1);
				}
			} else {
				/* error */
				gf_log ("switch", GF_LOG_ERROR, 
					"Check \"scheduler.switch.case\" "
					"option in unify volume. Exiting");
				free (switch_buf->array);
				free (switch_buf);
				return -1;
			}
			free (dup_str);
			switch_str = strtok_r (NULL, ";", &tmp_str);
		}
	}
	/* Now, all the pattern based considerations done, so for all the 
	 * remaining pattern, '*' to all the remaining child nodes
	 */
	{
		struct switch_sched_struct *switch_opt = NULL;
		int32_t flag = 0;
		int32_t index = 0;
		for (index=0; index < switch_buf->child_count; index++) {
			/* check for considered flag */
			if (switch_buf->array[index].considered)
				continue;
			flag++;
		}
		if (!flag) {
			gf_log ("switch", GF_LOG_ERROR,
				"No nodes left for pattern '*'. Exiting.");
			return -1;
		}
		switch_opt = CALLOC (1, sizeof (struct switch_sched_struct));
		ERR_ABORT (switch_opt);
		if (switch_buf->cond) {
			/* there are already few entries */
			struct switch_sched_struct *trav = switch_buf->cond;
			while (trav->next)
				trav = trav->next;
			trav->next = switch_opt;
		} else {
			/* First entry */
			switch_buf->cond = switch_opt;
		}
		/* Add the '*' pattern to the array */
		memcpy (switch_opt->path_pattern, "*", 2);
		switch_opt->num_child = flag;
		switch_opt->array = 
			CALLOC (1, flag * sizeof (struct switch_sched_array));
		ERR_ABORT (switch_opt->array);
		flag = 0;
		for (index=0; index < switch_buf->child_count; index++) {
			/* check for considered flag */
			if (switch_buf->array[index].considered)
				continue;
			gf_log ("switch", GF_LOG_DEBUG,
				"'%s' pattern will be scheduled to \"%s\"",
				switch_opt->path_pattern, 
				switch_buf->array[index].xl->name);
			switch_opt->array[flag].xl = 
				switch_buf->array[index].xl;
			switch_buf->array[index].considered = 1;
			flag++;
		}
	}

	pthread_mutex_init (&switch_buf->switch_mutex, NULL);

	// put it at the proper place
	*((long *)xl->private) = (long)switch_buf; 

	return 0;
}

static void
switch_fini (xlator_t *xl)
{
	/* TODO: free all the allocated entries */
	struct switch_struct *switch_buf = NULL;
	switch_buf = (struct switch_struct *)*((long *)xl->private);

	pthread_mutex_destroy (&switch_buf->switch_mutex);
	free (switch_buf->array);
	free (switch_buf);
}

static xlator_t *
switch_schedule (xlator_t *xl, const void *path)
{
	struct switch_struct *switch_buf = NULL;
	switch_buf = (struct switch_struct *)*((long *)xl->private);

	return switch_get_matching_xl (path, switch_buf->cond);
}


/**
 * notify
 */
void
switch_notify (xlator_t *xl, int32_t event, void *data)
{
	/* TODO: This should be checking in switch_sched_struct */
#if 0
	struct switch_struct *switch_buf = NULL;
	int32_t idx = 0;

	switch_buf = (struct switch_struct *)*((long *)xl->private);
	if (!switch_buf)
		return;

	for (idx = 0; idx < switch_buf->child_count; idx++) {
		if (switch_buf->array[idx].xl == (xlator_t *)data)
			break;
	}

	switch (event)
	{
	case GF_EVENT_CHILD_UP:
	{
		switch_buf->array[idx].eligible = 1;
	}
	break;
	case GF_EVENT_CHILD_DOWN:
	{
		switch_buf->array[idx].eligible = 0;
	}
	break;
	default:
	{
		;
	}
	break;
	}
#endif
}

static void 
switch_update (xlator_t *xl)
{
	return;
}

struct sched_ops sched = {
	.init     = switch_init,
	.fini     = switch_fini,
	.update   = switch_update,
	.schedule = switch_schedule,
	.notify   = switch_notify
};

struct volume_options options[] = {
	{ .key   = { "scheduler.read-only-subvolumes" ,
		     "switch.read-only-subvolumes"},  
	  .type  = GF_OPTION_TYPE_ANY
	},
	{ .key   = { "scheduler.local-volume-name",
		     "switch.nufa.local-volume-name" },
	  .type  = GF_OPTION_TYPE_XLATOR
	},
	{ .key   = { "scheduler.switch.case",
		     "switch.case" },
	  .type  = GF_OPTION_TYPE_ANY
	},
	{ .key = {NULL} }
};
