
#include "layout.h"
#include "xlator.h"

layout_t *
default_getlayout (struct xlator *xl,
		   layout_t *layout)
{
  chunk_t *chunk = &layout->chunks;

  layout->chunk_count = 1;
  
  chunk->path = layout->path;
  chunk->path_dyn = 0;
  chunk->begin = 0;
  chunk->end = -1;
  chunk->child = xl->first_child;
  chunk->next = NULL;

  return layout;
}


layout_t *
default_setlayout (struct xlator *xl,
		   layout_t *layout)
{
  chunk_t *chunk = &layout->chunks;

  layout->chunk_count = 1;
  
  chunk->path = layout->path;
  chunk->path_dyn = 0;
  chunk->begin = 0;
  chunk->end = -1;
  chunk->child = xl->first_child;
  chunk->next = NULL;

  return layout;
}

int
default_open (struct xlator *xl,
	      const char *path,
	      int flags,
	      mode_t mode,
	      struct file_context *ctx)
{
  layout_t layout = LAYOUT_INITIALIZER;
  chunk_t *chunk;
  int final_ret = 0;
  int ret = 0;
  int final_errno = 0;

  layout.path = (char *)path;
  xl->getlayout (xl, &layout);
  chunk = &layout.chunks;

  while (chunk) {
    ret = chunk->child->fops->open (chunk->child,
				    chunk->path,
				    flags,
				    mode,
				    ctx);
    if (ret != 0) {
      final_ret = -1;
      final_errno = errno;
    }
    chunk = chunk->next;
  }

  layout_unref (&layout);
  errno = final_errno;
  return final_ret;
}


int
default_chmod (struct xlator *xl,
	       const char *path,
	       mode_t mode)
{
  layout_t layout;
  chunk_t *chunk;
  int final_ret = 0;
  int ret = 0;
  int final_errno = 0;

  layout.path = (char *)path;
  xl->getlayout (xl, &layout);
  chunk = &layout.chunks;

  while (chunk) {
    ret = chunk->child->fops->chmod (chunk->child,
				     chunk->path,
				     mode);
    if (ret != 0) {
      final_ret = -1;
      final_errno = errno;
    }
    chunk = chunk->next;
  }
  
  layout_unref (&layout);
  errno = final_errno;
  return final_ret;
}
	       
