#ifndef _DICT_H
#define _DICT_H


struct _data {
  int len;
  char *data;
};
typedef struct _data data_t;

struct _data_pair {
  struct _data_pair *next;
  data_t *key;
  data_t *value;
};
typedef struct _data_pair data_pair_t;

struct _dict {
  int count;
  data_pair_t *members;
};
typedef struct _dict dict_t;

int is_data_equal (data_t *one, data_t *two);
void data_destroy (data_t *data);

int dict_set (dict_t *this, data_t *key, data_t *value);
data_t *dict_get (dict_t *this, data_t *key);
void dict_del (dict_t *this, data_t *key);

void dict_dump (FILE *fp, dict_t *dict);
dict_t *dict_load (FILE *fp);
void dict_destroy (dict_t *dict);

data_t *int_to_data (int value);
data_t *str_to_data (char *value);
data_t *bin_to_data (void *value, int len);

int data_to_int (data_t *data);
char *data_to_str (data_t *data);
void *data_to_bin (data_t *data);

#endif
