/*
   Copyright (c) 2007-2009 Gluster, Inc. <http://www.gluster.com>
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

#include <fnmatch.h>
#include "authenticate.h"

auth_result_t gf_auth (dict_t *input_params, dict_t *config_params)
{
  char *username = NULL, *password = NULL;
  data_t *allow_user = NULL, *username_data = NULL, *password_data = NULL;
  int32_t result = AUTH_DONT_CARE;
  char *brick_name = NULL, *searchstr = NULL;
  
  username_data = dict_get (input_params, "username");
  if (!username_data) 
    return AUTH_DONT_CARE;

  username = data_to_str (username_data);

  password_data = dict_get (input_params, "password");
  if (!password_data)
    return AUTH_DONT_CARE;

  password = data_to_str (password_data);

  brick_name = data_to_str (dict_get (input_params, "remote-subvolume"));
  if (!brick_name) {
    gf_log ("auth/login",
	    GF_LOG_ERROR,
	    "remote-subvolume not specified");
    return AUTH_REJECT;
  }

  asprintf (&searchstr, "auth.login.%s.allow", brick_name);
  allow_user = dict_get (config_params,
			 searchstr);
  free (searchstr);

  if (allow_user) {
    char *username_str = NULL;
    char *tmp;
    char *username_cpy = strdup (allow_user->data);
    
    username_str = strtok_r (username_cpy, " ,", &tmp);
      
    while (username_str) {
      data_t *passwd_data = NULL;
      if (!fnmatch (username_str,
		    username,
		    0)) {
	asprintf (&searchstr, "auth.login.%s.password", username);
	passwd_data = dict_get (config_params, searchstr);
        FREE (searchstr);

	if (!passwd_data) {
	  gf_log ("auth/login",
		  GF_LOG_DEBUG,
		  "wrong username/password combination");
	  result = AUTH_REJECT;
	}
	else 
	  result = !strcmp (data_to_str (passwd_data), password) ? AUTH_ACCEPT : AUTH_REJECT;
	break;
      }
      username_str = strtok_r (NULL, " ,", &tmp);  
    }
    free (username_cpy);
  }

  return result;
}

struct volume_options options[] = {
 	{ .key   = {"auth.login.*.allow"}, 
	  .type  = GF_OPTION_TYPE_ANY 
	},
 	{ .key   = {"auth.login.*.password"}, 
	  .type  = GF_OPTION_TYPE_ANY 
	},
	{ .key = {NULL} }
};
