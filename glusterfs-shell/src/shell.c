#include <sys/resource.h>

#include "shell.h"


/* interface_init:
 * Initializes the readline interface
 */
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
  while (line = readline (GPROMPT)){
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


/* do_main:
 * @argc - argument count, as recieved by the main()
 * @argv - argument vector, as recieved by the main()
 */
static void
do_main (int argc, char **argv)
{
  char *script = NULL;
  int i = 1;
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


int
main (int argc, char **argv)
{
  struct rlimit dump_core;
  dump_core.rlim_cur = RLIM_INFINITY;
  dump_core.rlim_max = RLIM_INFINITY;
  setrlimit (RLIMIT_CORE, &dump_core);

  gh_enter (argc, argv, do_main);
  return 1;
}
