#ifndef _BRICK_H
#define _BRICK_H

#include <stdio.h>
#include <arpa/inet.h>

struct wait_queue {
  struct wait_queue *next;
  pthread_mutex_t mutex;
};

struct brick_private {
  int sock;
  FILE *sock_fp;
  unsigned char connected;
  unsigned char is_debug;
  in_addr_t addr;
  unsigned short port;
  pthread_mutex_t mutex; /* mutex to fall in line in *queue */
  struct wait_queue *queue;
};

#endif
