/* strchrnul.c
 *
 */

/* Written by Niels Möller <nisse@lysator.liu.se>
 *
 * This file is hereby placed in the public domain.
 */

/* FIXME: What is this function supposed to do? My guess is that it is
 * like strchr, but returns a pointer to the NUL character, not a NULL
 * pointer, if the character isn't found. */

char *strchrnul(const char *, int );

char *strchrnul(const char *s, int c)
{
  const char *p = s;
  while (*p && (*p != c))
    p++;

  return (char *) p;
}
