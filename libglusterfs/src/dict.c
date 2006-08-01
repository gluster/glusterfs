#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "dict.h"

data_pair_t *
get_new_data_pair ()
{
  return (data_pair_t *) calloc (1, sizeof (data_pair_t));
}

data_t *
get_new_data ()
{
  return (data_t *) calloc (1, sizeof (data_t));
}

dict_t *
get_new_dict ()
{
  return (dict_t *) calloc (1, sizeof (dict_t));
}

void *
memdup (void *old, 
	int len)
{
  void *newdata = calloc (1, len);
  memcpy (newdata, old, len);
  return newdata;
}

int
is_data_equal (data_t *one,
	      data_t *two)
{
  if (one == two)
    return 1;

  if (one->len != two->len)
    return 0;

  if (one->data == two->data)
    return 1;

  if (memcmp (one->data, two->data, one->len) == 0)
    return 1;

  return 0;
}

void
data_destroy (data_t *data)
{
  if (data) {
    if (!data->is_static && data->data)
      free (data->data);

    if (!data->is_const)
      free (data);
  }
}

data_t *
data_copy (data_t *old)
{
  data_t *newdata = (data_t *) calloc (1, sizeof (*newdata));
  if (old) {
    newdata->len = old->len;
    if (old->data)
      newdata->data = memdup (old->data, old->len);
  }

  return newdata;
}

int
dict_set (dict_t *this, 
	  data_t *key, 
	  data_t *value)
{
  data_pair_t *pair = this->members;
  int count = this->count;

  while (count) {
    if (is_data_equal (pair->key, key)) {
      data_destroy (pair->key);
      data_destroy (pair->value);
      pair->key = key;
      pair->value = value;
      return 0;
    }
    pair = pair->next;
    count--;
  }

  pair = (data_pair_t *) calloc (1, sizeof (*pair));
  pair->key = (key);
  pair->value = (value);
  pair->next = this->members;
  this->members = pair;
  this->count++;

  return 0;
}

data_t *
dict_get (dict_t *this,
	  data_t *key)
{
  data_pair_t *pair = this->members;

  while (pair) {
    if (is_data_equal (pair->key, key))
      return (pair->value);
    pair = pair->next;
  }
  return NULL;
}

void
dict_del (dict_t *this,
	  data_t *key)
{
  data_pair_t *pair = this->members;
  data_pair_t *prev = NULL;

  while (pair) {
    if (is_data_equal (pair->key, key)) {
      if (prev)
	prev->next = pair->next;
      else
	this->members = pair->next;
      data_destroy (pair->key);
      data_destroy (pair->value);
      free (pair);
      this->count --;
      return;
    }
    prev = pair;
    pair = pair->next;
  }
  return;
}

void
dict_destroy (dict_t *this)
{
  data_pair_t *pair = this->members;
  data_pair_t *prev = this->members;

  while (prev) {
    pair = pair->next;
    data_destroy (prev->key);
    data_destroy (prev->value);
    free (prev);
    prev = pair;
  }

  if (!this->is_static)
    free (this);
  return;
}

int
dict_dump (FILE *fp,
	   dict_t *dict)
{
  data_pair_t *pair = dict->members;
  int count = dict->count;

  fprintf (fp, "%x", dict->count);
  while (count) {
    fprintf (fp, "\n%x:%x:", pair->key->len, pair->value->len);
    fwrite (pair->key->data, pair->key->len, 1, fp);
    fwrite (pair->value->data, pair->value->len, 1, fp);
    pair = pair->next;
    count--;
  }
  return 0;
}

dict_t *
dict_fill (FILE *fp, dict_t *fill)
{

  int ret = 0;
  int cnt = 0;

  ret = fscanf (fp, "%x", &fill->count);
  if (!ret)
    goto err;
  
  if (fill->count == 0)
    goto err;

  for (cnt = 0; cnt < fill->count; cnt++) {
    data_pair_t *pair = get_new_data_pair ();
    data_t *key = get_new_data ();
    data_t *value = get_new_data ();

    pair->key = key;
    pair->value = value;
    pair->next = fill->members;
    fill->members = pair;
    
    ret = fscanf (fp, "\n%x:%x:", &key->len, &value->len);
    if (ret != 2)
      goto err;

    key->data = malloc (key->len+1);
    ret = fread (key->data, key->len, 1, fp);
    if (!ret)
      goto err;
    key->data[key->len] = 0;

    value->data = malloc (value->len+1);
    ret = fread (value->data, value->len, 1, fp);
    if (!ret)
      goto err;
    value->data[value->len] = 0;
  }

  goto ret;

 err:
  dict_destroy (fill);
  fill = NULL;

 ret:
  return fill;
}

dict_t *
dict_load (FILE *fp)
{
  dict_t *newdict = get_new_dict ();
  int ret = 0;
  int cnt = 0;

  ret = fscanf (fp, "%x", &newdict->count);
  if (!ret)
    goto err;
  
  if (newdict->count == 0)
    goto err;

  for (cnt = 0; cnt < newdict->count; cnt++) {
    data_pair_t *pair = get_new_data_pair ();
    data_t *key = get_new_data ();
    data_t *value = get_new_data ();

    pair->key = key;
    pair->value = value;
    pair->next = newdict->members;
    newdict->members = pair;
    
    ret = fscanf (fp, "\n%x:%x:", &key->len, &value->len);
    if (ret != 2)
      goto err;

    key->data = malloc (key->len+1);
    ret = fread (key->data, key->len, 1, fp);
    if (!ret)
      goto err;
    key->data[key->len] = 0;
    
    value->data = malloc (value->len+1);
    ret = fread (value->data, value->len, 1, fp);
    if (!ret)
      goto err;
    value->data[value->len] = 0;
  }

  goto ret;

 err:
  dict_destroy (newdict);
  newdict = NULL;

 ret:
  return newdict;
}

data_t *
int_to_data (int value)
{
  data_t *data = get_new_data ();
  /*  if (data == NULL) {
    data = get_new_data ();
    data->data = NULL;
  }
  if (data->data == NULL)
    data->data = malloc (32);
  */
  asprintf (&data->data, "%d", value);
  data->len = strlen (data->data) + 1;
  return data;
}

data_t *
str_to_data (char *value)
{
  data_t *data = get_new_data ();
  /*  if (data == NULL) {
    data = get_new_data ();
    data->data = NULL;
  }
  if (data->data == NULL)
  */
  data->len = strlen (value) + 1;
  /*  data->data = malloc (data->len); */
  /* strcpy (data->data, value); */
  data->data = value;
  data->is_static = 1;
  return data;
}

data_t *
bin_to_data (void *value, int len)
{
  data_t *data = get_new_data ();
  /*
  static int data_len = 64*1024;
  if (data == NULL) {
    data = get_new_data ();
    data->data = NULL;
  }
  if (data->data == NULL)
    data->data = malloc (64 * 1024);
  if (len > data_len) {
    free (data->data);
    data->data = malloc (len);
    data_len = len;
  }
  */
  /*  data->data = memdup (value, len); */
  data->is_static = 1;
  data->len = len;
  data->data = value;
  return data;
}

int
data_to_int (data_t *data)
{
  return atoi (data->data);
}

char *
data_to_str (data_t *data)
{
  /*  return strdup (data->data); */
  return data->data;
}

void *
data_to_bin (data_t *data)
{/*
  static void *ret = NULL;
  static data_len = 64*1024;
  if (ret == NULL)
    ret = malloc (64 * 1024);
  if (data->len > data_len) {
    free (ret);
    ret = malloc (data->len);
    data_len = data->len;
  }
  memcpy (ret, data->data, data->len);
  return ret;
 */
  /*  return  memdup (data->data,  data->len);
   */
  return data->data;
}

static data_t _op           = { 3, "OP", 1, 1};
static data_t _path         = { 5, "PATH", 1, 1};
static data_t _offset       = { 7, "OFFSET", 1, 1}; 
static data_t _fd           = { 3, "FD", 1, 1};
static data_t _buf          = { 4, "BUF", 1, 1};
static data_t _count        = { 6, "COUNT", 1, 1};
static data_t _flags        = { 6, "FLAGS", 1, 1};
static data_t _errno        = { 6, "ERRNO", 1, 1};
static data_t _ret          = { 4, "RET", 1, 1};
static data_t _mode         = { 5, "MODE", 1, 1};
static data_t _dev          = { 4, "DEV", 1, 1};
static data_t _uid          = { 4, "UID", 1, 1};
static data_t _gid          = { 4, "GID", 1, 1};
static data_t _actime       = { 7, "ACTIME", 1, 1};
static data_t _modtime      = { 8, "MODTIME", 1, 1};
static data_t _len          = { 4, "LEN", 1, 1};

data_t * DATA_OP      = &_op;
data_t * DATA_PATH    = &_path;
data_t * DATA_OFFSET  = &_offset;
data_t * DATA_FD      = &_fd;
data_t * DATA_BUF     = &_buf;
data_t * DATA_COUNT   = &_count;
data_t * DATA_FLAGS   = &_flags;
data_t * DATA_ERRNO   = &_errno;
data_t * DATA_RET     = &_ret;
data_t * DATA_MODE    = &_mode;
data_t * DATA_DEV     = &_dev;
data_t * DATA_UID     = &_uid;
data_t * DATA_GID     = &_gid;
data_t * DATA_ACTIME  = &_actime;
data_t * DATA_MODTIME = &_modtime;
data_t * DATA_LEN     = &_len;
