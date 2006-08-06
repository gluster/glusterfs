#include "shell.h"
#include "extensions.h"

void
extensions_init (void)
{
  register_hooks ();
  register_primitives ();
}

void
gf_init (void)
{
  /* Display welcome message */
  gh_eval_str ("(display \"Glusterfs Shell!!!\")");
  gh_eval_str ("(newline)");

  
  /* initiate the extension channels */
  extensions_init ();

  /* load the scm file */
  gf_load ("../extensions/hello.scm");
  gf_load ("../extensions/commands.scm");
  
}
