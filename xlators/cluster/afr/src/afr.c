/*
   Copyright (c) 2007-2010 Gluster, Inc. <http://www.gluster.com>
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

#include <libgen.h>
#include <unistd.h>
#include <fnmatch.h>
#include <sys/time.h>
#include <stdlib.h>
#include <signal.h>

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif
#include "afr-common.c"

int32_t
notify (xlator_t *this, int32_t event,
	void *data, ...)
{
        int ret = -1;

        ret = afr_notify (this, event, data);

        return ret;
}

int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        if (!this)
                return ret;

        ret = xlator_mem_acct_init (this, gf_afr_mt_end + 1);

        if (ret != 0) {
                gf_log(this->name, GF_LOG_ERROR, "Memory accounting init"
                                "failed");
                return ret;
        }

        return ret;
}
int
validate_options (xlator_t *this, dict_t *options, char **op_errstr)
{


        gf_boolean_t metadata_self_heal;
        gf_boolean_t entry_self_heal;
        gf_boolean_t data_self_heal;
        gf_boolean_t data_change_log;
        gf_boolean_t metadata_change_log;
        gf_boolean_t entry_change_log;
        gf_boolean_t strict_readdir;
        gf_boolean_t optimistic_change_log;

        xlator_list_t * trav        = NULL;

        char * read_subvol     = NULL;
        char * self_heal       = NULL;
        char * change_log      = NULL;
        char * str_readdir     = NULL;
        char * self_heal_algo  = NULL;

        int32_t background_count  = 0;
        int32_t window_size       = 0;

        int    read_ret      = -1;
        int    dict_ret      = -1;
        int    flag          = 1;
        int    ret           = 0;
        int    temp_ret      = -1;



        dict_ret = dict_get_int32 (options, "background-self-heal-count",
                                   &background_count);
        if (dict_ret == 0) {
                if (background_count < 0) {
                        *op_errstr = gf_strdup ("Error, option should be >= 0");
                        ret = -1;
                        goto out;
                }

                gf_log (this->name, GF_LOG_DEBUG,
                        "validated background self-heal count to %d",
                        background_count);
        }

        dict_ret = dict_get_str (options, "metadata-self-heal",
                                 &self_heal);
        if (dict_ret == 0) {
                temp_ret = gf_string2boolean (self_heal, &metadata_self_heal);
                if (temp_ret < 0) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "validation failed 'option metadata"
                                                "-self-heal %s'.not correct.",
                                                self_heal);
                        *op_errstr = gf_strdup ("Error, option should be boolean");
                        ret = -1;
                        goto out;
                }

        }

        dict_ret = dict_get_str (options, "data-self-heal",
                                 &self_heal);
        if (dict_ret == 0) {
                temp_ret = gf_string2boolean (self_heal, &data_self_heal);
                if (temp_ret < 0) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "Validation failed for data self heal "
                                "(given-string = %s)", self_heal);
                        *op_errstr = gf_strdup ("Error, option should be "
                                                "boolean");
                        ret = -1;
                        goto out;
                }


                gf_log (this->name, GF_LOG_DEBUG,
                        "Reconfiguring 'option data"
                        "-self-heal %s'.", self_heal);
        }

        dict_ret = dict_get_str (options, "entry-self-heal",
                                 &self_heal);
        if (dict_ret == 0) {
                temp_ret = gf_string2boolean (self_heal, &entry_self_heal);
                if (temp_ret < 0) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "Validation faled for entry-self-heal"
                                " (given string = %s)",
                                self_heal);
                        *op_errstr = gf_strdup ("Error, option should be "
                                                "boolean");
                        ret = -1;
                        goto out;
                }


                gf_log (this->name, GF_LOG_DEBUG,
                        "Validated 'option entry"
                        "-self-heal %s'.", self_heal);
        }


        dict_ret = dict_get_str (options, "strict-readdir",
                                 &str_readdir);
        if (dict_ret == 0) {
                temp_ret = gf_string2boolean (str_readdir, &strict_readdir);
                if (temp_ret < 0) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "Validation faled for strict_readdir "
                                "(given-string = %s)", str_readdir);
                        *op_errstr = gf_strdup ("Error, option should be "
                                                "boolean");
                        ret = -1;
                        goto out;
                }


                gf_log (this->name, GF_LOG_DEBUG,
                        "Validated 'option strict"
                        "-readdir %s'.", str_readdir);
        }

        dict_ret = dict_get_int32 (options, "data-self-heal-window-size",
                                   &window_size);
        if (dict_ret == 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "validated data self-heal window size to %d",
                        window_size);

                if (window_size < 1) {
                        *op_errstr = gf_strdup ("Error, option should be >= 1");
                        ret = -1;
                        goto out;
                }

                if (window_size > 1024) {
                        *op_errstr = gf_strdup ("Error, option should be <= 1024");
                        ret = -1;
                        goto out;
                }


        }

        dict_ret = dict_get_str (options, "data-change-log",
                                 &change_log);
        if (dict_ret == 0) {
                temp_ret = gf_string2boolean (change_log, &data_change_log);
                if (temp_ret < 0) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "Validation faled for data-change-log");
                        *op_errstr = gf_strdup ("Error, option should be "
                                                "boolean");
                        ret = -1;
                        goto out;
                }


                gf_log (this->name, GF_LOG_DEBUG,
                        "Validated 'option data-"
                        "change-log %s'.", change_log);
        }

        dict_ret = dict_get_str (options, "metadata-change-log",
                                 &change_log);
        if (dict_ret == 0) {
                temp_ret = gf_string2boolean (change_log,
                                              &metadata_change_log);
                if (temp_ret < 0) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "Validation faild for metadata-change-log");
                        *op_errstr = gf_strdup ("Error, option should be "
                                                "boolean");
                        ret = -1;
                        goto out;
                }


                gf_log (this->name, GF_LOG_DEBUG,
                        "Validated 'option metadata-"
                        "change-log %s'.", change_log);
        }

        dict_ret = dict_get_str (options, "entry-change-log",
                                 &change_log);
        if (dict_ret == 0) {
                temp_ret = gf_string2boolean (change_log, &entry_change_log);
                if (temp_ret < 0) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "Validation faild for entr-change-log");
                        *op_errstr = gf_strdup ("Error, option should be "
                                                "boolean");
                        ret = -1;
                        goto out;
                }


                gf_log (this->name, GF_LOG_DEBUG,
                        "Validated 'option entry-"
                                        "change-log %s'.", change_log);
        }


        dict_ret = dict_get_str (options, "optimistic-change-log",
                                 &change_log);
        if (dict_ret == 0) {
                temp_ret = gf_string2boolean (change_log,
                                              &optimistic_change_log);
                if (temp_ret < 0) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "Validation faled for optimistic-change-log");
                        *op_errstr = gf_strdup ("Error, option should be "
                                                "boolean");
                        ret = -1;
                        goto out;
                }


                gf_log (this->name, GF_LOG_DEBUG,
                        "Validated 'option optimistic-"
                        "change-log %s'.", change_log);
        }

        dict_ret = dict_get_str (options, "data-self-heal-algorithm",
                                 &self_heal_algo);
        if (dict_ret == 0) {
                /* Handling both strcmp cases - s1 > s2 and s1 < s2 */

                if (!strcmp (self_heal_algo, "full"))
                        goto next;
                if (!strcmp (self_heal_algo, "diff"))
                        goto next;

                gf_log (this->name, GF_LOG_ERROR,
                        "Invalid self-heal algorithm %s",
                        self_heal_algo);
                *op_errstr = gf_strdup ("Error, invalid self-heal "
                                        "algorithm");
                ret = -1;
                goto out;
        }

        read_ret = dict_get_str (options, "read-subvolume", &read_subvol);

        if (read_ret)
                goto next;// No need to traverse, hence set the next option

        trav = this->children;
        flag = 0;
        while (trav) {
                if (!read_ret && !strcmp (read_subvol, trav->xlator->name)) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "Validated Subvolume '%s'  as read child.",
                                trav->xlator->name);
                        flag = 1;
                        ret = 0;
                        goto out;
                }
                trav = trav->next;
        }

        if (flag == 0 ) {
                gf_log (this->name, GF_LOG_WARNING,
                        "Invalid 'option read-subvolume %s', no such subvolume"
                        , read_subvol);
                *op_errstr = gf_strdup ("Error, the sub-volume is not right");
                ret = -1;
                goto out;
        }



next:
out:
        return ret;
}


int
reconfigure (xlator_t *this, dict_t *options)
{

	gf_boolean_t metadata_self_heal;   /* on/off */
	gf_boolean_t entry_self_heal;
	gf_boolean_t data_self_heal;
	gf_boolean_t data_change_log;       /* on/off */
	gf_boolean_t metadata_change_log;   /* on/off */
	gf_boolean_t entry_change_log;      /* on/off */
	gf_boolean_t strict_readdir;

	afr_private_t * priv        = NULL;
	xlator_list_t * trav        = NULL;

	char * read_subvol     = NULL;
	char * self_heal       = NULL;
	char * change_log      = NULL;
	char * str_readdir     = NULL;
        char * self_heal_algo  = NULL;

        int32_t background_count  = 0;
        int32_t window_size       = 0;

	int    read_ret      = -1;
	int    dict_ret      = -1;
	int    flag	     = 1;
	int    ret           = 0;
	int    temp_ret	     = -1;

	priv = this->private;

	dict_ret = dict_get_int32 (options, "background-self-heal-count",
				   &background_count);
	if (dict_ret == 0) {
		gf_log (this->name, GF_LOG_DEBUG,
			"Reconfiguring background self-heal count to %d",
			background_count);

		priv->background_self_heal_count = background_count;
	}

	dict_ret = dict_get_str (options, "metadata-self-heal",
				 &self_heal);
	if (dict_ret == 0) {
		temp_ret = gf_string2boolean (self_heal, &metadata_self_heal);
		if (temp_ret < 0) {
			gf_log (this->name, GF_LOG_WARNING,
				"Reconfiguration Invalid 'option metadata"
				"-self-heal %s'. Defaulting to old value.",
				self_heal);
			ret = -1;
			goto out;
		}

		priv->metadata_self_heal = metadata_self_heal;
		gf_log (this->name, GF_LOG_DEBUG,
			"Reconfiguring 'option metadata"
			"-self-heal %s'.",
			self_heal);
	}

	dict_ret = dict_get_str (options, "data-self-heal",
				 &self_heal);
	if (dict_ret == 0) {
		temp_ret = gf_string2boolean (self_heal, &data_self_heal);
		if (temp_ret < 0) {
			gf_log (this->name, GF_LOG_WARNING,
				"Reconfiguration Invalid 'option data"
				"-self-heal %s'. Defaulting to old value.",
				self_heal);
			ret = -1;
			goto out;
		}

		priv->data_self_heal = data_self_heal;
		gf_log (this->name, GF_LOG_DEBUG,
			"Reconfiguring 'option data"
			"-self-heal %s'.", self_heal);
	}

	dict_ret = dict_get_str (options, "entry-self-heal",
				 &self_heal);
	if (dict_ret == 0) {
		temp_ret = gf_string2boolean (self_heal, &entry_self_heal);
		if (temp_ret < 0) {
			gf_log (this->name, GF_LOG_WARNING,
				"Reconfiguration Invalid 'option data"
				"-self-heal %s'. Defaulting to old value.",
				self_heal);
			ret = -1;
			goto out;
		}

		priv->entry_self_heal = entry_self_heal;
		gf_log (this->name, GF_LOG_DEBUG,
			"Reconfiguring 'option entry"
			"-self-heal %s'.", self_heal);
	}


	dict_ret = dict_get_str (options, "strict-readdir",
				 &str_readdir);
	if (dict_ret == 0) {
		temp_ret = gf_string2boolean (str_readdir, &strict_readdir);
		if (temp_ret < 0) {
			gf_log (this->name, GF_LOG_WARNING,
				"Invalid 'option strict-readdir %s'. "
				"Defaulting to old value.",
				str_readdir);
			ret = -1;
			goto out;
		}

		priv->strict_readdir = strict_readdir;
		gf_log (this->name, GF_LOG_DEBUG,
			"Reconfiguring 'option strict"
			"-readdir %s'.", str_readdir);
	}

	dict_ret = dict_get_int32 (options, "data-self-heal-window-size",
				   &window_size);
	if (dict_ret == 0) {
		gf_log (this->name, GF_LOG_DEBUG,
			"Reconfiguring, Setting data self-heal window size to %d",
			window_size);

		priv->data_self_heal_window_size = window_size;
	}
        else {
                priv->data_self_heal_window_size = 16;
        }


	dict_ret = dict_get_str (options, "data-change-log",
				 &change_log);
	if (dict_ret == 0) {
		temp_ret = gf_string2boolean (change_log, &data_change_log);
		if (temp_ret < 0) {
			gf_log (this->name, GF_LOG_WARNING,
				"Reconfiguration Invalid 'option data-"
				"change-log %s'. Defaulting to old value.",
				change_log);
			ret = -1;
			goto out;
		}

		priv->data_change_log = data_change_log;
		gf_log (this->name, GF_LOG_DEBUG,
			"Reconfiguring 'option data-"
			"change-log %s'.", change_log);
	}

	dict_ret = dict_get_str (options, "metadata-change-log",
				 &change_log);
	if (dict_ret == 0) {
		temp_ret = gf_string2boolean (change_log,
					 &metadata_change_log);
		if (temp_ret < 0) {
			gf_log (this->name, GF_LOG_WARNING,
				"Invalid 'option metadata-change-log %s'. "
				"Defaulting to metadata-change-log as 'off'.",
				change_log);
			ret = -1;
			goto out;
		}

		priv->metadata_change_log = metadata_change_log;
		gf_log (this->name, GF_LOG_DEBUG,
			"Reconfiguring 'option metadata-"
			"change-log %s'.", change_log);
	}

	dict_ret = dict_get_str (options, "entry-change-log",
				 &change_log);
	if (dict_ret == 0) {
		temp_ret = gf_string2boolean (change_log, &entry_change_log);
		if (temp_ret < 0) {
			gf_log (this->name, GF_LOG_WARNING,
				"Invalid 'option entry-change-log %s'. "
				"Defaulting to entry-change-log as 'on'.",
				change_log);
			ret = -1;
			goto out;
		}

		priv->entry_change_log = entry_change_log;
		gf_log (this->name, GF_LOG_DEBUG,
			"Reconfiguring 'option entry-"
			"change-log %s'.", change_log);
	}

        dict_ret = dict_get_str (options, "data-self-heal-algorithm",
                                 &self_heal_algo);
        if (dict_ret == 0) {
                /* Handling both strcmp cases - s1 > s2 and s1 < s2 */

                if (!strcmp (self_heal_algo, "full")) {
                        priv->data_self_heal_algorithm = self_heal_algo;
                        gf_log (this->name, GF_LOG_DEBUG,
                                "Reconfiguring 'option data-self"
                                "heal-algorithm %s'.", self_heal_algo);
                        goto next;
                }

                if (!strcmp (self_heal_algo, "diff")) {
                        priv->data_self_heal_algorithm = self_heal_algo;
                        gf_log (this->name, GF_LOG_DEBUG,
                                "Reconfiguring 'option data-self"
                                "heal-algorithm %s'.", self_heal_algo);
                        goto next;
                }

                gf_log (this->name, GF_LOG_WARNING,
                        "Invalid self-heal algorithm %s,"
                        "defaulting back to old value",
                        self_heal_algo);
                ret = -1;
                goto out;
        }

	read_ret = dict_get_str (options, "read-subvolume", &read_subvol);

	if (read_ret < 0)
		goto next;// No need to traverse, hence set the next option

	trav = this->children;
	flag = 0;
	while (trav) {
		if (!read_ret && !strcmp (read_subvol, trav->xlator->name)) {
			gf_log (this->name, GF_LOG_DEBUG,
				"Subvolume '%s' specified as read child.",
				trav->xlator->name);

			flag = 1;
			ret = -1;
			goto out;
		}


		trav = trav->next;
	}

	if (flag == 0 ) {

		gf_log (this->name, GF_LOG_ERROR,
			"Invalid 'option read-subvolume %s', no such subvolume"
			, read_subvol);
		ret = -1;
		goto out;
	}


next:
out:
	return ret;

}


static const char *favorite_child_warning_str = "You have specified subvolume '%s' "
	"as the 'favorite child'. This means that if a discrepancy in the content "
	"or attributes (ownership, permission, etc.) of a file is detected among "
	"the subvolumes, the file on '%s' will be considered the definitive "
	"version and its contents will OVERWRITE the contents of the file on other "
	"subvolumes. All versions of the file except that on '%s' "
	"WILL BE LOST.";

static const char *no_lock_servers_warning_str = "You have set lock-server-count = 0. "
	"This means correctness is NO LONGER GUARANTEED in all cases. If two or more "
	"applications write to the same region of a file, there is a possibility that "
	"its copies will be INCONSISTENT. Set it to a value greater than 0 unless you "
	"are ABSOLUTELY SURE of what you are doing and WILL NOT HOLD GlusterFS "
	"RESPONSIBLE for inconsistent data. If you are in doubt, set it to a value "
	"greater than 0.";

int32_t
init (xlator_t *this)
{
	afr_private_t * priv        = NULL;
	int             child_count = 0;
	xlator_list_t * trav        = NULL;
	int             i           = 0;
	int             ret         = -1;
	int             op_errno    = 0;

	char * read_subvol     = NULL;
	char * fav_child       = NULL;
	char * self_heal       = NULL;
        char * algo            = NULL;
	char * change_log      = NULL;
	char * strict_readdir  = NULL;
        char * inodelk_trace   = NULL;
        char * entrylk_trace   = NULL;

        int32_t background_count  = 0;
	int32_t lock_server_count = 1;
        int32_t window_size       = 0;

	int    fav_ret       = -1;
	int    read_ret      = -1;
	int    dict_ret      = -1;


	if (!this->children) {
		gf_log (this->name, GF_LOG_ERROR,
			"replicate translator needs more than one "
                        "subvolume defined.");
		return -1;
	}

	if (!this->parents) {
		gf_log (this->name, GF_LOG_WARNING,
			"Volume is dangling.");
	}


	ALLOC_OR_GOTO (this->private, afr_private_t, out);

	priv = this->private;

	read_ret = dict_get_str (this->options, "read-subvolume", &read_subvol);
	priv->read_child = -1;

	fav_ret = dict_get_str (this->options, "favorite-child", &fav_child);
	priv->favorite_child = -1;

        priv->background_self_heal_count = 16;

	dict_ret = dict_get_int32 (this->options, "background-self-heal-count",
				   &background_count);
	if (dict_ret == 0) {
		gf_log (this->name, GF_LOG_DEBUG,
			"Setting background self-heal count to %d",
			background_count);

		priv->background_self_heal_count = background_count;
	}

	/* Default values */

	priv->data_self_heal     = 1;
	priv->metadata_self_heal = 1;
	priv->entry_self_heal    = 1;

	dict_ret = dict_get_str (this->options, "data-self-heal", &self_heal);
	if (dict_ret == 0) {
		ret = gf_string2boolean (self_heal, &priv->data_self_heal);
		if (ret < 0) {
			gf_log (this->name, GF_LOG_WARNING,
				"Invalid 'option data-self-heal %s'. "
				"Defaulting to data-self-heal as 'on'",
				self_heal);
			priv->data_self_heal = 1;
		}
	}

        priv->data_self_heal_algorithm = "";

        dict_ret = dict_get_str (this->options, "data-self-heal-algorithm",
                                 &algo);
        if (dict_ret == 0) {
                priv->data_self_heal_algorithm = gf_strdup (algo);
        }


        priv->data_self_heal_window_size = 16;

	dict_ret = dict_get_int32 (this->options, "data-self-heal-window-size",
				   &window_size);
	if (dict_ret == 0) {
		gf_log (this->name, GF_LOG_DEBUG,
			"Setting data self-heal window size to %d",
			window_size);

		priv->data_self_heal_window_size = window_size;
	}

	dict_ret = dict_get_str (this->options, "metadata-self-heal",
				 &self_heal);
	if (dict_ret == 0) {
		ret = gf_string2boolean (self_heal, &priv->metadata_self_heal);
		if (ret < 0) {
			gf_log (this->name, GF_LOG_WARNING,
				"Invalid 'option metadata-self-heal %s'. "
				"Defaulting to metadata-self-heal as 'on'.",
				self_heal);
			priv->metadata_self_heal = 1;
		}
	}

	dict_ret = dict_get_str (this->options, "entry-self-heal", &self_heal);
	if (dict_ret == 0) {
		ret = gf_string2boolean (self_heal, &priv->entry_self_heal);
		if (ret < 0) {
			gf_log (this->name, GF_LOG_WARNING,
				"Invalid 'option entry-self-heal %s'. "
				"Defaulting to entry-self-heal as 'on'.",
				self_heal);
			priv->entry_self_heal = 1;
		}
	}

	/* Change log options */

	priv->data_change_log     = 1;
	priv->metadata_change_log = 1;
	priv->entry_change_log    = 1;
        priv->optimistic_change_log = 1;

	dict_ret = dict_get_str (this->options, "data-change-log",
				 &change_log);
	if (dict_ret == 0) {
		ret = gf_string2boolean (change_log, &priv->data_change_log);
		if (ret < 0) {
			gf_log (this->name, GF_LOG_WARNING,
				"Invalid 'option data-change-log %s'. "
				"Defaulting to data-change-log as 'on'.",
				change_log);
			priv->data_change_log = 1;
		}
	}

	dict_ret = dict_get_str (this->options, "metadata-change-log",
				 &change_log);
	if (dict_ret == 0) {
		ret = gf_string2boolean (change_log,
					 &priv->metadata_change_log);
		if (ret < 0) {
			gf_log (this->name, GF_LOG_WARNING,
				"Invalid 'option metadata-change-log %s'. "
				"Defaulting to metadata-change-log as 'off'.",
				change_log);
			priv->metadata_change_log = 0;
		}
	}

	dict_ret = dict_get_str (this->options, "entry-change-log",
				 &change_log);
	if (dict_ret == 0) {
		ret = gf_string2boolean (change_log, &priv->entry_change_log);
		if (ret < 0) {
			gf_log (this->name, GF_LOG_WARNING,
				"Invalid 'option entry-change-log %s'. "
				"Defaulting to entry-change-log as 'on'.",
				change_log);
			priv->entry_change_log = 1;
		}
	}

	dict_ret = dict_get_str (this->options, "optimistic-change-log",
				 &change_log);
	if (dict_ret == 0) {
		ret = gf_string2boolean (change_log, &priv->optimistic_change_log);
		if (ret < 0) {
			gf_log (this->name, GF_LOG_WARNING,
				"Invalid 'option optimistic-change-log %s'. "
				"Defaulting to optimistic-change-log as 'on'.",
				change_log);
			priv->optimistic_change_log = 1;
		}
	}

	/* Locking options */

        priv->inodelk_trace = 0;
        priv->entrylk_trace = 0;

	dict_ret = dict_get_str (this->options, "inodelk-trace",
				 &inodelk_trace);
	if (dict_ret == 0) {
		ret = gf_string2boolean (inodelk_trace, &priv->inodelk_trace);
		if (ret < 0) {
			gf_log (this->name, GF_LOG_WARNING,
				"Invalid 'option inodelk-trace %s' ",
                                inodelk_trace);

			priv->inodelk_trace = 0;
		}
	}


	dict_ret = dict_get_str (this->options, "entrylk-trace",
				 &entrylk_trace);
	if (dict_ret == 0) {
		ret = gf_string2boolean (entrylk_trace, &priv->entrylk_trace);
		if (ret < 0) {
			gf_log (this->name, GF_LOG_WARNING,
				"Invalid 'option entrylk-trace %s' ",
                                inodelk_trace);

			priv->entrylk_trace = 0;
		}
	}


	priv->data_lock_server_count = 1;
	priv->metadata_lock_server_count = 0;
	priv->entry_lock_server_count = 1;

	dict_ret = dict_get_int32 (this->options, "data-lock-server-count",
				   &lock_server_count);
	if (dict_ret == 0) {
		gf_log (this->name, GF_LOG_DEBUG,
			"Setting data lock server count to %d.",
			lock_server_count);

		if (lock_server_count == 0)
			gf_log (this->name, GF_LOG_WARNING, "%s",
                                no_lock_servers_warning_str);

		priv->data_lock_server_count = lock_server_count;
	}


	dict_ret = dict_get_int32 (this->options,
				   "metadata-lock-server-count",
				   &lock_server_count);
	if (dict_ret == 0) {
		gf_log (this->name, GF_LOG_DEBUG,
			"Setting metadata lock server count to %d.",
			lock_server_count);
		priv->metadata_lock_server_count = lock_server_count;
	}


	dict_ret = dict_get_int32 (this->options, "entry-lock-server-count",
				   &lock_server_count);
	if (dict_ret == 0) {
		gf_log (this->name, GF_LOG_DEBUG,
			"Setting entry lock server count to %d.",
			lock_server_count);

		priv->entry_lock_server_count = lock_server_count;
	}

	priv->strict_readdir = _gf_false;

	dict_ret = dict_get_str (this->options, "strict-readdir",
				 &strict_readdir);
	if (dict_ret == 0) {
		ret = gf_string2boolean (strict_readdir, &priv->strict_readdir);
		if (ret < 0) {
			gf_log (this->name, GF_LOG_WARNING,
				"Invalid 'option strict-readdir %s'. "
				"Defaulting to strict-readdir as 'off'.",
				strict_readdir);
		}
	}

	trav = this->children;
	while (trav) {
		if (!read_ret && !strcmp (read_subvol, trav->xlator->name)) {
			gf_log (this->name, GF_LOG_DEBUG,
				"Subvolume '%s' specified as read child.",
				trav->xlator->name);

			priv->read_child = child_count;
		}

		if (fav_ret == 0 && !strcmp (fav_child, trav->xlator->name)) {
			gf_log (this->name, GF_LOG_WARNING,
				favorite_child_warning_str, trav->xlator->name,
				trav->xlator->name, trav->xlator->name);
			priv->favorite_child = child_count;
		}

		child_count++;
		trav = trav->next;
	}

	priv->wait_count = 1;

	priv->child_count = child_count;

	LOCK_INIT (&priv->lock);
        LOCK_INIT (&priv->read_child_lock);

	priv->child_up = GF_CALLOC (sizeof (unsigned char), child_count,
                                    gf_afr_mt_char);
	if (!priv->child_up) {
		gf_log (this->name, GF_LOG_ERROR,
			"Out of memory.");
		ret = -ENOMEM;
		goto out;
	}

        for (i = 0; i < child_count; i++)
                priv->child_up[i] = -1; /* start with unknown state.
                                           this initialization needed
                                           for afr_notify() to work
                                           reliably
                                        */

	priv->children = GF_CALLOC (sizeof (xlator_t *), child_count,
                                    gf_afr_mt_xlator_t);
	if (!priv->children) {
		gf_log (this->name, GF_LOG_ERROR,
			"Out of memory.");
		ret = -ENOMEM;
		goto out;
	}

        priv->pending_key = GF_CALLOC (sizeof (*priv->pending_key),
                                        child_count,
                                        gf_afr_mt_char);
        if (!priv->pending_key) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Out of memory.");
                ret = -ENOMEM;
                goto out;
        }

	trav = this->children;
	i = 0;
	while (i < child_count) {
		priv->children[i] = trav->xlator;

                ret = gf_asprintf (&priv->pending_key[i], "%s.%s",
                                   AFR_XATTR_PREFIX,
                                   trav->xlator->name);
                if (-1 == ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "asprintf failed to set pending key");
                        ret = -ENOMEM;
                        goto out;
                }

		trav = trav->next;
		i++;
	}

        LOCK_INIT (&priv->root_inode_lk);
        priv->first_lookup = 1;
        priv->root_inode = NULL;

        pthread_mutex_init (&priv->mutex, NULL);
        INIT_LIST_HEAD (&priv->saved_fds);

	ret = 0;
out:
	return ret;
}


int
fini (xlator_t *this)
{
	return 0;
}


struct xlator_fops fops = {
	.lookup      = afr_lookup,
	.open        = afr_open,
	.lk          = afr_lk,
	.flush       = afr_flush,
	.statfs      = afr_statfs,
	.fsync       = afr_fsync,
	.fsyncdir    = afr_fsyncdir,
	.xattrop     = afr_xattrop,
	.fxattrop    = afr_fxattrop,
	.inodelk     = afr_inodelk,
	.finodelk    = afr_finodelk,
	.entrylk     = afr_entrylk,
	.fentrylk    = afr_fentrylk,

	/* inode read */
	.access      = afr_access,
	.stat        = afr_stat,
	.fstat       = afr_fstat,
	.readlink    = afr_readlink,
	.getxattr    = afr_getxattr,
	.readv       = afr_readv,

	/* inode write */
	.writev      = afr_writev,
	.truncate    = afr_truncate,
	.ftruncate   = afr_ftruncate,
	.setxattr    = afr_setxattr,
        .setattr     = afr_setattr,
	.fsetattr    = afr_fsetattr,
	.removexattr = afr_removexattr,

	/* dir read */
	.opendir     = afr_opendir,
	.readdir     = afr_readdir,
	.readdirp    = afr_readdirp,

	/* dir write */
	.create      = afr_create,
	.mknod       = afr_mknod,
	.mkdir       = afr_mkdir,
	.unlink      = afr_unlink,
	.rmdir       = afr_rmdir,
	.link        = afr_link,
	.symlink     = afr_symlink,
	.rename      = afr_rename,
};


struct xlator_dumpops dumpops = {
        .priv       = afr_priv_dump,
};


struct xlator_cbks cbks = {
	.release     = afr_release,
	.releasedir  = afr_releasedir,
};


struct volume_options options[] = {
	{ .key  = {"read-subvolume" },
	  .type = GF_OPTION_TYPE_XLATOR
	},
	{ .key  = {"favorite-child"},
	  .type = GF_OPTION_TYPE_XLATOR
	},
        { .key  = {"background-self-heal-count"},
          .type = GF_OPTION_TYPE_INT,
          .min  = 0
        },
	{ .key  = {"data-self-heal"},
	  .type = GF_OPTION_TYPE_BOOL
	},
        { .key  = {"data-self-heal-algorithm"},
          .type = GF_OPTION_TYPE_STR
        },
        { .key  = {"data-self-heal-window-size"},
          .type = GF_OPTION_TYPE_INT,
          .min  = 1,
          .max  = 1024
        },
	{ .key  = {"metadata-self-heal"},
	  .type = GF_OPTION_TYPE_BOOL
	},
	{ .key  = {"entry-self-heal"},
	  .type = GF_OPTION_TYPE_BOOL
	},
	{ .key  = {"data-change-log"},
	  .type = GF_OPTION_TYPE_BOOL
	},
	{ .key  = {"metadata-change-log"},
	  .type = GF_OPTION_TYPE_BOOL
	},
	{ .key  = {"entry-change-log"},
	  .type = GF_OPTION_TYPE_BOOL
	},
	{ .key  = {"optimistic-change-log"},
	  .type = GF_OPTION_TYPE_BOOL
	},
	{ .key  = {"data-lock-server-count"},
	  .type = GF_OPTION_TYPE_INT,
	  .min  = 0
	},
	{ .key  = {"metadata-lock-server-count"},
	  .type = GF_OPTION_TYPE_INT,
	  .min  = 0
	},
	{ .key  = {"entry-lock-server-count"},
	  .type = GF_OPTION_TYPE_INT,
	  .min  = 0
	},
	{ .key  = {"strict-readdir"},
	  .type = GF_OPTION_TYPE_BOOL,
	},
	{ .key  = {NULL} },
};
