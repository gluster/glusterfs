#ifndef _DEFAULTS_H
#define _DEFAULTS_H

#include "layout.h"
#include "xlator.h"

int
default_getlayout (struct xlator *this,
		   layout_t *layout);

int
default_setlayout (struct xlator *this,
		   layout_t *layout);

int
default_open (struct xlator *this,
	      const char *path,
	      int flags,
	      mode_t mode,
	      struct file_context *ctx);
int
default_chmod (struct xlator *this,
	       const char *path,
	       mode_t mode);

#endif /* _DEFAULTS_H */
