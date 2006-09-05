#include "primitives.h"
#include "extensions.h"

SCM
gf_hello (SCM scm_string)
{
  char *string = SCM_STRING_CHARS (scm_string);
  printf ("you %s", string);
  return SCM_UNSPECIFIED;
}


SCM
gf_demo (SCM scm_string)
{
  char *string = SCM_STRING_CHARS (scm_string);
  char *pstring = NULL;
  char *append = "dingdong";

  pstring = calloc (strlen (string) + strlen (append) + 1, 1);
  sprintf (pstring, "%s----%s", string, append);

  scm_run_hook (ex_gf_hi, gh_list (gh_str02scm (pstring),
				      SCM_UNDEFINED));
  free (pstring);

  return SCM_UNDEFINED;
}
