#ifndef _XPORT_SOCKET_H
#define _XPORT_SOCKET_H

#include <stdio.h>
#include <arpa/inet.h>

#define CLIENT_PORT_CIELING 1023
struct wait_queue {
  struct wait_queue *next;
  pthread_mutex_t mutex;
};

struct brick_private {
  int sock;
  int addr_family;
  FILE *sock_fp;
  unsigned char connected;
  unsigned char is_debug;
  in_addr_t addr;
  unsigned short port;
  char *volume;
  pthread_mutex_t mutex; /* mutex to fall in line in *queue */
  pthread_mutex_t io_mutex;
  struct wait_queue *queue;
};

#endif
