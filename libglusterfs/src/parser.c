#include "xlator.h"

int
file_to_xlator_tree (FILE *fp)
{ 
  struct xlator *tree;
  
  char single_line[4096];

  while (fgets (single_line, 4096, fp)) {
    char *first_word;
  }
  
  return ret;
}
