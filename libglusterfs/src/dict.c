#include <unistd.h>
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
	  char *key, 
	  data_t *value)
{
  data_pair_t *pair = this->members;
  int count = this->count;

  while (count) {
    if (strcmp (pair->key, key) == 0) {
      data_destroy (pair->value);
      if (strlen (pair->key) < strlen (key))
	pair->key = realloc (pair->key, strlen (key) + 1);
      strcpy (pair->key, key);
      pair->value = value;
      return 0;
    }
    pair = pair->next;
    count--;
  }

  pair = (data_pair_t *) calloc (1, sizeof (*pair));
  pair->key = (char *) calloc (1, strlen (key) + 1);
  strcpy (pair->key, key);
  pair->value = (value);
  pair->next = this->members;
  this->members = pair;
  this->count++;

  return 0;
}


int
dict_case_set (dict_t *this, 
	  char *key, 
	  data_t *value)
{
  data_pair_t *pair = this->members;
  int count = this->count;

  while (count) {
    if (strcasecmp (pair->key, key) == 0) {
      data_destroy (pair->value);
      if (strlen (pair->key) < strlen (key))
	pair->key = realloc (pair->key, strlen (key) + 1);
      strcpy (pair->key, key);
      pair->value = value;
      return 0;
    }
    pair = pair->next;
    count--;
  }

  pair = (data_pair_t *) calloc (1, sizeof (*pair));
  pair->key = (char *) calloc (1, strlen (key) + 1);
  strcpy (pair->key, key);
  pair->value = (value);
  pair->next = this->members;
  this->members = pair;
  this->count++;

  return 0;
}

data_t *
dict_get (dict_t *this,
	  char *key)
{
  data_pair_t *pair = this->members;

  while (pair) {
    if (strcmp (pair->key, key) == 0)
      return (pair->value);
    pair = pair->next;
  }
  return NULL;
}


data_t *
dict_case_get (dict_t *this,
	       char *key)
{
  data_pair_t *pair = this->members;

  while (pair) {
    if (strcasecmp (pair->key, key) == 0)
      return (pair->value);
    pair = pair->next;
  }
  return NULL;
}

void
dict_del (dict_t *this,
	  char *key)
{
  data_pair_t *pair = this->members;
  data_pair_t *prev = NULL;

  while (pair) {
    if (strcmp (pair->key, key) == 0) {
      if (prev)
	prev->next = pair->next;
      else
	this->members = pair->next;
      data_destroy (pair->value);
      free (pair->key);
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
dict_case_del (dict_t *this,
	       char *key)
{
  data_pair_t *pair = this->members;
  data_pair_t *prev = NULL;

  while (pair) {
    if (strcasecmp (pair->key, key) == 0) {
      if (prev)
	prev->next = pair->next;
      else
	this->members = pair->next;
      data_destroy (pair->value);
      free (pair->key);
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
    data_destroy (prev->value);
    free (prev->key);
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
    fprintf (fp, "\n%x:%x:", strlen (pair->key), pair->value->len);
    fwrite (pair->key, strlen (pair->key), 1, fp);
    fwrite (pair->value->data, pair->value->len, 1, fp);
    pair = pair->next;
    count--;
  }
  return 0;
}

/*
  Serialization format:
  ----
  Count:4
  Key_len:4:Value_len:4
  Key
  Value
  .
  .
  .
*/

int
dict_serialized_length (dict_t *dict)
{
  int len = 5; /* count + \n */
  int count = dict->count;
  data_pair_t *pair = dict->members;

  while (count) {
    len += 10 + strlen (pair->key) + pair->value->len;
    pair = pair->next;
    count--;
  }

  return len;
}

void
dict_serialize (dict_t *dict, char *buf)
{
  data_pair_t *pair = dict->members;
  int count = dict->count;

  // FIXME: magic numbers
  sprintf (buf, "%04o\n", dict->count);
  buf += 5;
  while (count) {
    sprintf (buf, "%04o:%04o\n", strlen (pair->key), pair->value->len);
    buf += 10;
    memcpy (buf, pair->key, strlen (pair->key));
    buf += strlen (pair->key);
    memcpy (buf, pair->value->data, pair->value->len);
    pair = pair->next;
    count--;
  }
}

dict_t *
dict_unserialize (int fd, int size, dict_t *fill)
{
  char *buf = malloc (size);
  char *p = buf;
  int bytes_read = 0;

  if (size < 0)
    goto err;
  
  while (bytes_read < size) {
    int ret = read (fd, p, size - bytes_read);
    if (ret <= 0) { /* we can't have EOF right after a dict */
      free (buf);
      goto err;
    }
    
    bytes_read += ret;
    p += bytes_read;
  }

  int ret = 0;
  int cnt = 0;

  ret = sscanf (buf, "%o\n", &fill->count);
  if (!ret)
    goto err;
  buf += 5;
  
  if (fill->count == 0)
    goto err;

  for (cnt = 0; cnt < fill->count; cnt++) {
    data_pair_t *pair = NULL; //get_new_data_pair ();
    data_t *value = NULL; // = get_new_data ();
    char *key = NULL;
    int key_len, value_len;
    
    ret = sscanf (buf, "%o:%o\n", &key_len, &value_len);
    if (ret != 2)
      goto err;
    buf += 10;

    key = calloc (1, key_len + 1);
    memcpy (key, buf, key_len);
    buf += key_len;
    key[key_len] = 0;

    value = get_new_data ();
    value->len = value_len;
    value->data = malloc (value->len+1);

    pair = get_new_data_pair ();
    pair->key = key;
    pair->value = value;

    pair->next = fill->members;
    fill->members = pair;

    if (value->len < value_len) {
      int count = value_len - value->len;
      memcpy (value->data, buf, value->len);
      buf += count;
    } else {
      memcpy (value->data, buf, value_len);
      buf += value_len;
    }
    if (!ret)
      goto err;
    value->data[value->len] = 0;
  }

  goto ret;

 err:
  dict_destroy (fill);
  fill = NULL;

 ret:
  return NULL;
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
    data_pair_t *pair = NULL; //get_new_data_pair ();
    data_t *value = NULL; // = get_new_data ();
    char *key = NULL;
    int key_len, value_len;

    
    ret = fscanf (fp, "\n%x:%x:", &key_len, &value_len);
    if (ret != 2)
      goto err;
    
    key = calloc (1, key_len + 1);
    ret = fread (key, key_len, 1, fp);
    if (!ret) {
      free (key);
      goto err;
    }
    key[key_len] = 0;

    {
      /*data_t *preset_value;*/
      data_pair_t *preset_pair = fill->members;

      while (preset_pair) {
	if (strcmp (key, preset_pair->key) == 0) {
	  value = preset_pair->value;
	  break;
	}
	preset_pair = preset_pair->next;
      } 

      if (!preset_pair) {
	value = get_new_data ();
	value->len = value_len;
	value->data = malloc (value->len+1);

	pair = get_new_data_pair ();
	pair->key = key;
	pair->value = value;

	pair->next = fill->members;
	fill->members = pair;
      }
    }

    if (value->len < value_len) {
      int count = value_len - value->len;
      int x = 0;
      ret = fread (value->data, value->len, 1, fp);
      while (count--)
	fread ((void *)&x, 1, 1, fp);
    } else {
      ret = fread (value->data, value_len, 1, fp);
    }
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
    data_t *value = get_new_data ();
    char *key = NULL;
    int key_len = 0;

    ret = fscanf (fp, "\n%x:%x:", &key_len, &value->len);
    if (ret != 2)
      goto err;
    
    key = calloc (1, key_len + 1);
    ret = fread (key, key_len, 1, fp);
    if (!ret)
      goto err;
    key[key_len] = 0;
    
    value->data = malloc (value->len+1);
    ret = fread (value->data, value->len, 1, fp);
    if (!ret)
      goto err;
    value->data[value->len] = 0;

    pair->key = key;
    pair->value = value;
    pair->next = newdict->members;
    newdict->members = pair;
  }

  goto ret;

 err:
  dict_destroy (newdict);
  newdict = NULL;

 ret:
  return newdict;
}

data_t *
int_to_data (long long int value)
{
  data_t *data = get_new_data ();
  /*  if (data == NULL) {
    data = get_new_data ();
    data->data = NULL;
  }
  if (data->data == NULL)
    data->data = malloc (32);
  */
  asprintf (&data->data, "%lld", value);
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

long long int
data_to_int (data_t *data)
{
  if (!data)
    return -1;

  return atoll (data->data);
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
  if (data)
    return data->data;

  return NULL;
}

void
dict_foreach (dict_t *dict,
	      void (*fn)(dict_t *this, char *key, data_t *value))
{
  data_pair_t *pairs = dict->members;

  while (pairs) {
    fn (dict, pairs->key, pairs->value);
    pairs = pairs->next;
  }
}
