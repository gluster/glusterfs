#include "interpreter.h"

int
interpreter (char *line)
{
  char *cmd, *args;
 
  cmd = strtok (line, " ");
  if (!cmd)
    return 1;
  args = strtok (NULL, "\0");
  
  scm_run_hook (ex_gf_command_hook, gh_list (gh_str02scm (cmd),
						gh_str02scm (args),
						SCM_UNDEFINED));

  return 1;  
}
