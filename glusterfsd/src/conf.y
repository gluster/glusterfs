%token DIR_NAME KEY_LENGTH NEWLINE VALUE WHITESPACE COMMENT CHROOT SCRATCH NUMBER NUMBER_BYTE PORT ID PROTOCOL

%{
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "glusterfsd.h"

static void set_chroot_dir (char *dir);
static void set_scratch_dir (char *dir);
static void set_key_len (char *key);
static int  set_port_num (char *port);
static void set_inet_prot (char *prot);

#define YYSTYPE char *

%}

%%
C1: C1 C2 | C2;
C2: KEY_LEN | PORT_NUM | SCRATCH_DIR | CHROOT_DIR | INET_PROT;

CHROOT_DIR: CHROOT ID {set_chroot_dir ($2);};
SCRATCH_DIR: SCRATCH ID {set_scratch_dir ($2);};
KEY_LEN: KEY_LENGTH ID {set_key_len ($2);};
PORT_NUM: PORT ID {set_port_num ($2);};
INET_PROT:  PROTOCOL ID {set_inet_prot ($2);};
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

static int 
set_port_num (char *port)
{
  char *port_str = NULL;
  unsigned int nport;
  char *delim = NULL;
  
  printf ("Listen = [%s]\n", port);
  
  port_str = strdupa (port);
  if (str2uint (port_str, 0, &nport) == 0)
    {
      complete_confd->port = nport;
      complete_confd->bind_ip_address = NULL;
      return 0;
    }
  if (validate_ip_address (port_str) == 0)
    {
      complete_confd->port = 0;
      complete_confd->bind_ip_address = strdup (port_str);
      return 0;
    }
  if ((delim = strchr (port_str, ':')))
    {
      delim[0] = '\0';
      
      if (validate_ip_address (port_str))
	{
	  complete_confd->bind_ip_address = NULL;
	  complete_confd->port = 0;
	  return (-1);
	}
      complete_confd->bind_ip_address = strdup (port_str);
      port_str = delim + 1;
      if (str2uint (port_str, 0, &nport))
	{
	  free (complete_confd->bind_ip_address);
	  complete_confd->bind_ip_address = NULL;
	  complete_confd->port = 0;
	  return (-1);
	}
      complete_confd->port = nport;
      return (0);
    }
  
  complete_confd->bind_ip_address = NULL;
  complete_confd->port = 0;
  return (-1);
}

static void 
set_inet_prot (char *prot)
{
  printf ("Interconnect protocol is %s\n", prot);
  complete_confd->inet_prot = strdup (prot);
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
