#ifndef _DEFAULTS_H
#define _DEFAULTS_H

#include "layout.h"
#include "xlator.h"

layout_t *
default_layout (struct xlator *this,
		const char *filename);

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
