#include <sys/resource.h>

#include "shell.h"
#include <argp.h>

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

error_t
parse_opts (int key, char *arg, struct argp_state *_state)
{
  switch (key){
  case 's':
    script = arg;
    break;
  }
  return 0;
}

void 
args_init (int argc, char **argv)
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
do_main (int argc, char **argv)
{

  int i = 1;
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
