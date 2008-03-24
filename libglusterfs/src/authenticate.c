/*
  Copyright (c) 2007, 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <dlfcn.h>
#include <errno.h>
#include "authenticate.h"

static void
init (dict_t *this,
      char *key,
      data_t *value,
      void *data)
{
  void *handle = NULL;
  char *auth_file = NULL;
  auth_handle_t *auth_handle = NULL;
  auth_fn_t authenticate = NULL;

  asprintf (&auth_file, "%s/%s.so", LIBDIR, key);
  handle = dlopen (auth_file, RTLD_LAZY);
  if (!handle) {
    gf_log ("authenticate", GF_LOG_ERROR, "dlopen(%s): %s\n", 
	    auth_file, dlerror ());
    dict_set (this, key, data_from_dynptr (NULL, 0));
    free (auth_file);
    return;
  }
  free (auth_file);
  
  authenticate = dlsym (handle, "gf_auth");
  if (!authenticate) {
    gf_log ("authenticate", GF_LOG_ERROR,
	    "dlsym(gf_auth) on %s\n", dlerror ());
    dict_set (this, key, data_from_dynptr (NULL, 0));
    return;
  }

  auth_handle = calloc (1, sizeof (*auth_handle));
  if (!auth_handle) {
    gf_log ("authenticate", GF_LOG_ERROR, "Out of memory");
    dict_set (this, key, data_from_dynptr (NULL, 0));
    return;
  }

  auth_handle->authenticate = authenticate;
  auth_handle->handle = handle;

  dict_set (this, key, data_from_dynptr (auth_handle, sizeof (*auth_handle)));
}

static void
fini (dict_t *this,
      char *key,
      data_t *value,
      void *data)
{
  auth_handle_t *handle = data_to_ptr (value);
  if (handle) {
    dlclose (handle->handle);
  }
}

int32_t
gf_auth_init (dict_t *auth_modules)
{
  int32_t error = 0;

  dict_foreach (auth_modules, init, &error);
  if (error) {
    int32_t dummy;
    dict_foreach (auth_modules, fini, &dummy);
  }

  return error;
}

static dict_t *__input_params;
static dict_t *__config_params;

void 
map (dict_t *this,
     char *key,
     data_t *value,
     void *data)
{
  dict_t *res = data;
  auth_fn_t authenticate;
  auth_handle_t *handle = NULL;

  if (value && (handle = data_to_ptr (value)) && 
      (authenticate = handle->authenticate)) {
    dict_set (res, key, 
	      int_to_data (authenticate (__input_params, __config_params)));
  } else {
    dict_set (res, key, int_to_data (AUTH_DONT_CARE));
  }
}

void 
reduce (dict_t *this,
	char *key,
	data_t *value,
	void *data)
{
  int64_t val = 0;
  int64_t *res = data;
  if (!data)
    return;

  val = data_to_int64 (value);
  switch (val)
    {
    case AUTH_ACCEPT:
      if (AUTH_DONT_CARE == *res)
	*res = AUTH_ACCEPT;
      break;

    case AUTH_REJECT:
      *res = AUTH_REJECT;
      break;

    case AUTH_DONT_CARE:
      break;
    }
}

 
auth_result_t 
gf_authenticate (dict_t *input_params, 
		 dict_t *config_params, 
		 dict_t *auth_modules) 
{
  dict_t *results = NULL;
  int64_t result = AUTH_DONT_CARE;

  results = get_new_dict ();
  __input_params = input_params;
  __config_params = config_params;

  dict_foreach (auth_modules, map, results);

  dict_foreach (results, reduce, &result);
  if (AUTH_DONT_CARE == result) {
    char *name = NULL;
    name = data_to_str (dict_get (input_params, "remote-subvolume"));
    gf_log ("auth", GF_LOG_ERROR,
	    "Nobody cares to authenticate!! Rejecting the client %s", name);
    result = AUTH_REJECT;
  }
    
  dict_destroy (results);
  return result;
}

void 
gf_auth_fini (dict_t *auth_modules)
{
  int32_t dummy;

  dict_foreach (auth_modules, fini, &dummy);
}
