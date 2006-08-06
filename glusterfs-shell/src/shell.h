#ifndef _GFYSH_H_
#define _GFYSH_H_

#include <stdio.h>
#include <guile/gh.h>
#include <readline/readline.h>

#include "primitives.h"
#include "extensions.h"
#include "interpreter.h"

/* prompt to display */
#define GPROMPT "gf:O "

#define FALSE 0
#define TRUE  1

void gf_init (void);

#endif /* _GFYSH_H_ */
