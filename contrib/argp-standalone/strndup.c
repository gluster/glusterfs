/* strndup.c
 *
 */

/* Written by Niels Möller <nisse@lysator.liu.se>
 *
 * This file is hereby placed in the public domain.
 */

#include <stdlib.h>
#include <string.h>

char *
strndup (const char *, size_t);

char *
strndup (const char *s, size_t size)
{
  char *r;
  char *end = memchr(s, 0, size);
  
  if (end)
    /* Length + 1 */
    size = end - s + 1;
  
  r = malloc(size);

  if (size)
    {
      memcpy(r, s, size-1);
      r[size-1] = '\0';
    }
  return r;
}
