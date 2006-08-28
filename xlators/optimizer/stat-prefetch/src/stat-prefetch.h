#ifndef _GETATTR_H_
#define _GETATTR_H_

#include <stdio.h>
#include <sys/time.h>
//#include <any_other_required_header>

struct getattr_node {
  struct getattr_node *next;
  struct stat *stbuf;
  char *pathname;
};

struct getattr_private {
  int temp;
  char is_debug;
  pthread_mutex_t mutex; 
  struct timeval curr_tval;
  struct timeval timeout;
  struct getattr_node *head;
};

#endif /* _GETATTR_H_ */
