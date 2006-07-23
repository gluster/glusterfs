#ifndef _DICT_H
#define _DICT_H


struct _data {
  int len;
  char *data;
  char is_static;
  char is_const;
};
typedef struct _data data_t;

struct _data_pair {
  struct _data_pair *next;
  data_t *key;
  data_t *value;
};
typedef struct _data_pair data_pair_t;

struct _dict {
  char is_static;
  int count;
  data_pair_t *members;
};
typedef struct _dict dict_t;

int is_data_equal (data_t *one, data_t *two);
void data_destroy (data_t *data);

int dict_set (dict_t *this, data_t *key, data_t *value);
data_t *dict_get (dict_t *this, data_t *key);
void dict_del (dict_t *this, data_t *key);

int dict_dump (FILE *fp, dict_t *dict);
dict_t *dict_load (FILE *fp);
dict_t *dict_fill (FILE *fp, dict_t *dict);
void dict_destroy (dict_t *dict);

data_t *int_to_data (int value);
data_t *str_to_data (char *value);
data_t *bin_to_data (void *value, int len);
data_t *static_str_to_data (char *value);
data_t *static_bin_to_data (void *value);

int data_to_int (data_t *data);
char *data_to_str (data_t *data);
void *data_to_bin (data_t *data);

data_t *get_new_data ();
dict_t *get_new_dict ();
data_pair_t *get_new_data_pair ();

#define STATIC_DICT {1, 0, NULL};
#define STATIC_DATA_STR(str) {strlen (str) + 1, str, 1, 1};

#endif
