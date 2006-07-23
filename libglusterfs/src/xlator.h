#ifndef _XLATOR_H
#define _XLATOR_H
#include <stdio.h>
#include "dict.h"
//#include "schedule.h"

struct xlator;

struct xlator_fops {
  int (*open) (struct xlator *this,
	       const char *path,
	       int flags);
};

struct xlator {
  char *name;
  struct xlator *next; /* for maintainence */
  struct xlator *parent;
  struct xlator *first_child;
  struct xlator *next_sibling;

  struct xlator_fops *fops;

  void (*fini) (struct xlator *this);
  void (*init) (struct xlator *this, void *data);

  dict_t *options;
  void *private;
};

struct xlator_fops *
type_to_fops (const char *type);
#endif
