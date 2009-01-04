/*
  Copyright (c) 2009 Z RESEARCH, Inc. <http://www.zresearch.com>
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
#include "map.h"


xlator_t *
map_subvol_next (xlator_t *this, xlator_t *prev)
{
	map_private_t *priv = NULL;
	xlator_t      *next = NULL;
	int            i = 0;

	priv = this->private;

	for (i = 0; i < priv->child_count; i++) {
		if (priv->xlarray[i].xl == prev) {
			if ((i + 1) < priv->child_count)
				next = priv->xlarray[i + 1].xl;
			break;
		}
	}

	return next;
}

int
map_subvol_cnt (xlator_t *this, xlator_t *subvol)
{
	int i = 0;
	int ret = -1;
	map_private_t *priv = NULL;

	priv = this->private;

	for (i = 0; i < priv->child_count; i++) {
		if (subvol == priv->xlarray[i].xl) {
			ret = i;
			break;
		}
	}

	return ret;
}

int
map_itransform (xlator_t *this, xlator_t *subvol, uint64_t x, uint64_t *y_p)
{
	map_private_t *priv = NULL;
	int         cnt = 0;
	int         max = 0;
	uint64_t    y = 0;

	if (x == ((uint64_t) -1)) {
		y = (uint64_t) -1;
		goto out;
	}

	priv = this->private;

	max = priv->child_count;
	cnt = map_subvol_cnt (this, subvol);

	y = ((x * max) + cnt);

out:
	if (y_p)
		*y_p = y;

	return 0;
}


int
map_deitransform (xlator_t *this, uint64_t y, xlator_t **subvol_p,
		  uint64_t *x_p)
{
	int         cnt = 0;
	int         max = 0;
	uint64_t    x = 0;
	xlator_t   *subvol = 0;
	map_private_t *priv = NULL;

	priv = this->private;
	max = priv->child_count;

	cnt = y % max;
	x   = y / max;

	subvol = priv->xlarray[cnt].xl;

	if (subvol_p)
		*subvol_p = subvol;

	if (x_p)
		*x_p = x;

	return 0;
}


xlator_t *
get_mapping_subvol_from_path (xlator_t *this, const char *path) 
{
	map_private_t      *priv = NULL;
	struct map_pattern *map = NULL;

	/* To make sure we handle '/' properly */
	if (!strcmp (path, "/"))
		return NULL;

	priv = this->private;

	map = priv->map;
	while (map) {
		if (!strncmp (map->directory, path, map->dir_len)) {
			if ((path[map->dir_len] == '/') ||
			    (path[map->dir_len] == '\0')) {
				return map->xl;
			}
		}
		
		map = map->next;
	}

	return priv->default_xl;
}

xlator_t *
get_mapping_subvol_from_ctx (xlator_t *this, dict_t *ctx)
{
	xlator_t *subvol = NULL;
	int       ret    = -1;

	ret = dict_get_ptr (ctx, this->name, VOID(&subvol));
	if (ret != 0) 
		return NULL;

	return subvol;
}

int
check_multiple_volume_entry (xlator_t *this, 
			     xlator_t *subvol)
{
	int ret = -1;
	int idx = 0;
	map_private_t *priv = NULL;

	priv = this->private;
	
	for (idx = 0; idx < priv->child_count; idx++) {
		if (priv->xlarray[idx].xl == subvol) {
			if (priv->xlarray[idx].mapped) {
				gf_log (this->name, GF_LOG_ERROR,
					"subvolume '%s' is already mapped",
					subvol->name);
				goto out;
			}
			priv->xlarray[idx].mapped = 1;
			ret = 0;
			goto out;
		}
	}

	gf_log (this->name, GF_LOG_ERROR,
		"subvolume '%s' is not found",
		subvol->name);
	
 out:
	return ret;
}

int
verify_dir_and_assign_subvol (xlator_t *this, 
			      const char *directory, 
			      const char *subvol)
{
	int            default_flag = 0;
	int            ret  = -1;
	int            idx  = 0;
	map_private_t *priv = NULL;
	xlator_list_t *trav = NULL;
	struct map_pattern *tmp_map = NULL;

	priv = this->private;

	/* check if directory is valid, ie, its a top level dir, and 
	 * not includes a '*' in it.
	 */
	if (!strcmp ("*", directory)) {
		default_flag = 1;
	} else {
		if (directory[0] != '/') {
			gf_log (this->name, GF_LOG_ERROR,
				"map takes absolute path, starting with '/'. "
				"not '%s'", directory);
			goto out;
		}
		for (idx = 1; idx < (strlen (directory) - 1); idx++) {
			if (directory[idx] == '/') {
				gf_log (this->name, GF_LOG_ERROR,
					"map takes only top level directory, "
					"not '%s'", directory);
				goto out;
			}
		}
	}

	/* Assign proper subvolume */
	trav = this->children;
	while (trav) {
		if (!strcmp (trav->xlator->name, subvol)) {
			
			/* Check if there is another directory for 
			 * same volume, if yes, return error.
			 */
			ret = check_multiple_volume_entry (this, 
							   trav->xlator);
			if (ret != 0) {
				goto out;
			}

			ret = 0;
			if (default_flag) {
				if (priv->default_xl) {
					ret = -1;
					gf_log (this->name, GF_LOG_ERROR,
						"'*' specified more than "
						"once. don't confuse me!!!");
				}

				priv->default_xl = trav->xlator;
				goto out;
			}

			tmp_map = CALLOC (1, sizeof (struct map_pattern));
			tmp_map->xl = trav->xlator;
			tmp_map->dir_len = strlen (directory);

			/* make sure that the top level directory starts 
			 * with '/' and ends without '/'
			 */
			tmp_map->directory = strdup (directory);
			if (directory[tmp_map->dir_len - 1] == '/') {
				tmp_map->dir_len--;
			}

			if (!priv->map) 
				priv->map = tmp_map;
			else {
				struct map_pattern *trav_map = NULL;
				trav_map = priv->map;
				while (trav_map->next)
					trav_map = trav_map->next;
				trav_map->next = tmp_map;
			}
			
			goto out;
		}

		trav = trav->next;
	}

	gf_log (this->name, GF_LOG_ERROR, 
		"map volume '%s' is not proper subvolume", subvol);

 out:
	return ret;
}

int 
assign_default_subvol (xlator_t *this, const char *default_xl)
{
	int ret = -1;
	map_private_t *priv = NULL;
	xlator_list_t *trav = NULL;

	priv = this->private;
	trav = this->children;

	while (trav) {
		if (!strcmp (trav->xlator->name, default_xl)) {
			ret = check_multiple_volume_entry (this, 
							   trav->xlator);
			if (ret != 0) {
				goto out;
			}
			if (priv->default_xl)
				gf_log (this->name, GF_LOG_WARNING,
					"default-volume option provided, "
					"overriding earlier '*' option");
			priv->default_xl = trav->xlator;
			return 0;
		}
		trav = trav->next;
	}

	gf_log (this->name, GF_LOG_ERROR,
		"default-volume value is not an valid subvolume. check again");
 out:
	return -1;
}

void
verify_if_all_subvolumes_got_used (xlator_t *this)
{
	int idx = 0;
	map_private_t *priv = NULL;

	priv = this->private;
	
	for (idx = 0; idx < priv->child_count; idx++) {
		if (!priv->xlarray[idx].mapped) {
			if (!priv->default_xl) {
				priv->default_xl = priv->xlarray[idx].xl;
				priv->xlarray[idx].mapped = 1;
			} else {
				gf_log (this->name, GF_LOG_WARNING,
					"subvolume '%s' is not mapped to "
					"any directory",
					priv->xlarray[idx].xl->name);
			}
		}
	}

	if (!priv->default_xl) {
		gf_log (this->name, GF_LOG_WARNING,
			"default subvolume not specified, filesystem "
			"may not work properly. Check 'map' translator "
			"documentation for more info");
	}

	return ;
}
