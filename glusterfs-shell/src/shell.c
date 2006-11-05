/*
  (C) 2006 Z RESEARCH Inc. <http://www.zresearch.com>
  
  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 2 of
  the License, or (at your option) any later version.
    
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.
    
  You should have received a copy of the GNU General Public
  License along with this program; if not, write to the Free
  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
  Boston, MA 02110-1301 USA
*/ 

#include <sys/resource.h>
#include <argp.h>
#include <stdint.h>

#include "shell.h"

/* interface_init:
 * Initializes the readline interface
 */
static char *script = NULL;
static void
interface_init (void)
{
  /* rl_attempted_completion_function = gf_auto_complete;
     rl_completion_entry_function = NULL;*/
}

scm_t_catch_handler 
catcher (void)
{
  printf ("No such command\n");
  return NULL;
}


/* gf_repl:
 * read-eval-play-loop
 */
static void
gf_repl (void)
{
  char *line = NULL;
  char *cmd_line = NULL;

  /* scheme eval loop :O */
  while ((line = readline (GPROMPT))){
    if (!line)
      break;
    /* call our interpreter to execute our commands */
    cmd_line = strdup (line);
    if (interpreter (cmd_line) != 0){
      /* interpreter failed, let guile interpret it */
      gh_eval_str_with_catch (line, (scm_t_catch_handler) catcher);
      scm_force_output (scm_current_output_port ());
    }
    free (cmd_line);
    free (line);
  }
}

error_t
parse_opts (int32_t key, char *arg, struct argp_state *_state)
{
  switch (key){
  case 's':
    script = arg;
    break;
  }
  return 0;
}

void 
args_init (int32_t argc, char **argv)
{
  struct {
    char *f[2];
  } f;
  static char doc[] = "glusterfs-shell is a management tool for the glusterfs";
  static char argp_doc[] = ":O";
  
  static struct argp_option options[] = {
    {"script", 's', "SCRIPTFILE", 0, "Execute the SCRIPTFILE" },
    { 0, }
  };
  static struct argp argp = { options, parse_opts, argp_doc, doc };
  
  argp_parse (&argp, argc, argv, 0, 0, &f);
}
/* do_main:
 * @argc - argument count, as recieved by the main()
 * @argv - argument vector, as recieved by the main()
 */
static void
do_main (int32_t argc, char **argv)
{

  int32_t i = 1;
  /* initialiaze the command line arguments using argp */
  args_init (argc, argv);

  /* initialize the readline interface */
  interface_init ();

  /* read configuration file and initialize the shell */
  gf_init ();

  while (i < argc){
    if (!strcmp (argv[i], "-s")){
      script = argv [++i];
      break;
    }
    i++;
  }

  if (!script){
    gf_repl ();
  } else {
    gf_load (script);
  }

  exit (0);
}


int32_t 
main (int32_t argc, char **argv)
{
  struct rlimit lim;
  lim.rlim_cur = RLIM_INFINITY;
  lim.rlim_max = RLIM_INFINITY;
  setrlimit (RLIMIT_CORE, &lim);
  setrlimit (RLIMIT_NOFILE, &lim);
#if 0
  if (gf_log_init ("/tmp/glusterfs-shell.log") < 0){
    fprintf (stderr, "error initialising the log file\n");
    return 1;
  }
  gf_log_set_loglevel (GF_LOG_DEBUG);
#endif
  gh_enter (argc, argv, do_main);
  return 1;
}
