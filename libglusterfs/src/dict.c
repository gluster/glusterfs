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
  return (dict_t *) calloc (0, sizeof (dict_t));
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
    if (data->data)
      free (data->data);
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
      if (is_data_equal (pair->value, value))
	return 0;
      data_destroy (pair->value);
      pair->value = data_copy (value);
    }
    pair = pair->next;
    count--;
  }

  pair = (data_pair_t *) calloc (1, sizeof (*pair));
  pair->key = data_copy (key);
  pair->value = data_copy (value);
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
      return data_copy (pair->value);
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

  return;
}

void
dict_dump (FILE *fp,
	   dict_t *dict)
{
  data_pair_t *pair = dict->members;
  int count = dict->count;

  fprintf (fp, "count=%x\n", dict->count);
  while (count) {
    fprintf (fp, "key:%x=", pair->key->len);
    fwrite (pair->key->data, pair->key->len, 1, fp);
    fprintf (fp, "\n");
    fprintf (fp, "value:%x=", pair->value->len);
    fwrite (pair->value->data, pair->value->len, 1, fp);
    fprintf (fp, "\n");
    pair = pair->next;
    count--;
  }
}

dict_t *
dict_load (FILE *fp)
{
  dict_t *newdict = get_new_dict ();
  int ret = 0;
  int cnt = 0;

  ret = fscanf (fp, "count=%x", &newdict->count);
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
    
    ret = fscanf (fp, "\nkey:%x=", &key->len);
    if (!ret)
      goto err;

    key->data = malloc (key->len+1);
    ret = fread (key->data, key->len + 1, 1, fp);
    if (!ret)
      goto err;
    key->data[key->len] = 0;
    
    ret = fscanf (fp, "\nvalue:%x=", &value->len);
    if (!ret)
      goto err;

    value->data = malloc (value->len+1);
    ret = fread (value->data, value->len + 1, 1, fp);
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
  static data_t *data = NULL;
  if (data == NULL) {
    data = get_new_data ();
    data->data = NULL;
  }
  if (data->data == NULL)
    data->data = malloc (32);
  sprintf (data->data, "%d", value);
  data->len = strlen (data->data);
  return data;
}

data_t *
str_to_data (char *value)
{
  static data_t *data = NULL;
  if (data == NULL) {
    data = get_new_data ();
    data->data = NULL;
  }
  if (data->data == NULL)
    data->data = malloc (256);
  sprintf (data->data, "%s", value);
  data->len = strlen (data->data);
  return data;
}

data_t *
bin_to_data (void *value, int len)
{
  static data_t *data = NULL;
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
  data->len = len;
  memcpy (data->data, value, len);
  return data;
}

int
data_to_int (data_t *data)
{
  int ret;
  sscanf (data->data, "%d", &ret);
  return ret;
}

char *
data_to_str (data_t *data)
{
  static char *ret = NULL;
  if(ret == NULL)
    ret = malloc (256);
  sscanf (data->data, "%s", &ret);
  return ret;
}

void *
data_to_bin (data_t *data)
{
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
}
