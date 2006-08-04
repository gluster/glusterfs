#ifndef _GETATTR_H_
#define _GETATTR_H_

#include <stdio.h>
//#include <any_other_required_header>

struct getattr_node {
  struct getattr_node *next;
  struct stat *stbuf;
  char *pathname;
};

struct getattr_private {
  int temp;
  char is_debug;
  struct getattr_node *head;
};

#endif /* _GETATTR_H_ */
