#ifndef _EXTENSIONS_H_
#define _EXTENSIONS_H_

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "shell.h"
#include "fops.h"
extern SCM ex_gf_hi;
extern SCM ex_gf_command_hook;

void gf_load (const char *file);
void register_hooks (void);
void register_primitives (void);
#endif /* _EXTENSIONS_H_ */
