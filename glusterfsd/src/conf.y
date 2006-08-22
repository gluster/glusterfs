%token DIR_NAME KEY_LENGTH NEWLINE VALUE WHITESPACE COMMENT CHROOT SCRATCH NUMBER NUMBER_BYTE PORT ID

%{
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "glusterfsd.h"

static void set_chroot_dir (char *dir);
static void set_scratch_dir (char *dir);
static void set_key_len (char *key);
static void set_port_num (char *port);

#define YYSTYPE char *

%}

%%
C1: C1 C2 | C2;
C2: KEY_LEN | PORT_NUM | SCRATCH_DIR | CHROOT_DIR;

CHROOT_DIR: CHROOT ID {set_chroot_dir ($2);};
SCRATCH_DIR: SCRATCH ID {set_scratch_dir ($2);};
KEY_LEN: KEY_LENGTH ID {set_key_len ($2);};
PORT_NUM: PORT ID {set_port_num ($2);};

%%

struct confd *complete_confd;

static void 
set_chroot_dir (char *dir)
{
  printf  ("chroot directory = %s\n", dir);
  complete_confd->chroot_dir = calloc (1, strlen (dir) + 1);
  strcpy(complete_confd->chroot_dir, dir);
}

static void 
set_scratch_dir (char *dir)
{
  printf  ("Scratch directory = %s\n", dir);
  complete_confd->scratch_dir = calloc (1, strlen (dir) + 1);
  strcpy(complete_confd->scratch_dir, dir);
}

static void 
set_key_len (char *key)
{
  printf  ("Maximum Key Length = %s\n", key);
  complete_confd->key_len = atoi (key);
}

static void 
set_port_num (char *port)
{
  printf  ("Listening Port = %s\n", port);
  complete_confd->port = atoi (port);
}

static void
parse_error (void)
{
  printf ("Config file parse error\n");
  if (complete_confd->chroot_dir)
    free (complete_confd->chroot_dir);
  if (complete_confd->scratch_dir)
    free (complete_confd->scratch_dir);
  free (complete_confd);
}

int
ccwrap ()
{
  return 1;
}

int 
ccerror (const char *str)
{
  parse_error ();
  complete_confd = NULL;
  fprintf (stderr, "error: %s\n", str);
  return 0;
}

extern FILE *ccin;
struct confd *
file_to_confd (FILE *fp)
{
  ccin = fp;
  complete_confd = calloc (1, sizeof (struct confd));
  ccparse ();
  return complete_confd;
}
