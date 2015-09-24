
## Symbol Versions and SO_NAMEs

  In general, adding new APIs to a shared library does not require that
symbol versions be used or the the SO_NAME be "bumped." These actions
are usually reserved for when a major change is introduced, e.g. many
APIs change or a signficant change in the functionality occurs.

  Over the normal lifetime of a When a new API is added, the library is
recompiled, consumers of the new API are able to do so, and existing,
legacy consumers of the original API continue as before. If by some
chance an old copy of the library is installed on a system, it's unlikely
that most applications will be affected. New applications that use the
new API will incur a run-time error terminate.

  Bumping the SO_NAME, i.e. changing the shared lib's file name, e.g.
from libfoo.so.0 to libfoo.so.1, which also changes the ELF SO_NAME
attribute inside the file, works a little differently. libfoo.so.0
contains only the old APIs. libfoo.so.1 contains both the old and new
APIs. Legacy software that was linked with libfoo.so.0 continues to work
as libfoo.so.0 is usually left installed on the system. New software that
uses the new APIs is linked with libfoo.so.1, and works as long as
long as libfoo.so.1 is installed on the system. Accidentally (re)installing
libfoo.so.0 doesn't break new software as long as reinstalling doesn't
erase libfoo.so.1.

  Using symbol versions is somewhere in the middle. The shared library
file remains libfoo.so.0 forever. Legacy APIs may or may not have an
associated symbol version. New APIs may or may not have an associated
symbol version either. In general symbol versions are reserved for APIs
that have changed. Either the function's signature has changed, i.e. the
return time or the number of paramaters, and/or the parameter types have
changed. Another reason for using symbol versions on an API is when the
behaviour or functionality of the API changes dramatically. As with a
library that doesn't use versioned symbols, old and new applications
either find or don't find the versioned symbols they need. If the versioned
symbol doesn't exist in the installed library, the application incurs a
run-time error and terminates.

  GlusterFS wanted to keep tight control over the APIs in libgfapi.
Originally bumping the SO_NAME was considered, and GlusterFS-3.6.0 was
released with libgfapi.so.7. Not only was "7" a mistake (it should have
been "6"), but it was quickly pointed out that many dependent packages
that use libgfapi would be forced to be recompiled/relinked. Thus no
packages of 3.6.0 were ever released and 3.6.1 was quickly released with
libgfapi.so.0, but with symbol versions. There's no strong technical
reason for either; the APIs have not changed, only new APIs have been
added. It's merely being done in anticipation that some APIs might change
sometime in the future.

  Enough about that now, let's get into the nitty gritty——

## Adding new APIs

### Adding a public API.

  This is the default, and the easiest thing to do. Public APIs have
declarations in either glfs.h, glfs-handles.h, or, at your discretion,
in a new header file intended for consumption by other developers.

Here's what you need to do to add a new public API:

+ Write the declaration, e.g. in glfs.h:

```C
    int glfs_dtrt (const char *volname, void *stuff) __THROW
```

+ Write the definition, e.g. in glfs-dtrt.c:

```C
   int
   pub_glfs_dtrt (const char *volname, void *stuff)
   {
       ...
       return 0;
   }
```

+ Add the symbol version magic for ELF, gnu toolchain to the definition.

  following the definition of your new function in glfs-dtrtops.c, add a
  line like this:

```C
  GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_dtrt, 3.7.0)
```

  The whole thing should look like:

```C
      int
      pub_glfs_dtrt (const char *volname, void *stuff)
      {
           ...
      }
      GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_dtrt, 3.7.0);
```

  In this example, 3.7.0 refers to the Version the symbol will first
  appear in. There's nothing magic about it, it's just a string token.
  The current versions we have are 3.4.0, 3.4.2, 3.5.0, 3.5.1, and 3.6.0.
  They are to be considered locked or closed. You can not, must not add
  any new APIs and use these versions. Most new APIs will use 3.7.0. If
  you add a new API appearing in 3.6.2 (and mainline) then you would use
  3.6.2.

+ Add the symbol version magic for OS X to the declaration.

  following the declaration in glfs.h, add a line like this:

```C
  GFAPI_PUBLIC(glfs_dtrt, 3.7.0)
```

  The whole thing should look like:

```C
     int glfs_dtrt (const char *volname, void *stuff) __THROW
              GFAPI_PUBLIC(glfs_dtrt, 3.7.0);
```

  The version here must match the version associated with the definition.

+ Add the new API to the ELF, gnu toolchain link map file, gfapi.map

  Most new public APIs will probably be added to a new section that
  looks like this:

```
  GFAPI_3.7.0 {
          global:
                  glfs_dtrt;
  } GFAPI_PRIVATE_3.7.0;
```

  if you're adding your new API to, e.g. 3.6.2, it'll look like this:

```
  GFAPI_3.6.2 {
          global:
                  glfs_dtrt;
  } GFAPI_3.6.0;
```

  and you must change the
```
    GFAPI_PRIVATE_3.7.0 { ...} GFAPI_3.6.0;
```
  section to:
```
    GFAPI_PRIVATE_3.7.0 { ...} GFAPI_3.6.2;
```

+ Add the new API to the OS X alias list file, gfapi.aliases.

  Most new APIs will use a line that looks like this:

```C
 _pub_glfs_dtrt _glfs_dtrt$GFAPI_3.7.0
```

  if you're adding your new API to, e.g. 3.6.2, it'll look like this:

```C
 _pub_glfs_dtrt _glfs_dtrt$GFAPI_3.6.2
```

And that's it.


### Adding a private API.

  If you're thinking about adding a private API that isn't declared in
one of the header files, then you should seriously rethink what you're
doing and figure out how to put it in libglusterfs instead.

If that hasn't convinced you, follow the instructions above, but use the
_PRIVATE versions of macros, symbol versions, and aliases. If you're 1337
enough to ignore this advice, then you're 1337 enough to figure out how
to do it.


## Changing an API.

### Changing a public API.

  There are two ways an API might change, 1) its signature has changed, or
2) its new functionality or behavior is substantially different than the
old. An APIs signature consists of the function return type, and the number
and/or type of its parameters. E.g. the original API:

```C
   int glfs_dtrt (const char *volname, void *stuff);
```

and the changed API:

```C
   void *glfs_dtrt (const char *volname, glfs_t *ctx, void *stuff);
```

  One way to avoid a change like this, and which is preferable in many
ways, is to leave the legacy glfs_dtrt() function alone, document it as
deprecated, and simply add a new API, e.g. glfs_dtrt2(). Practically
speaking, that's effectively what we'll be doing anyway, the difference
is only that we'll use a versioned symbol to do it.

  On the assumption that adding a new API is undesirable for some reason,
perhaps the use of glfs_gnu() is just so pervasive that we really don't
want to add glfs_gnu2().

+ change the declaration in glfs.h:

```C
  glfs_t *glfs_gnu (const char *volname, void *stuff) __THROW
          GFAPI_PUBLIC(glfs_gnu, 3.7.0);
````

Note that there is only the single, new declaration.

+ change the old definition of glfs_gnu() in glfs.c:

```C
    struct glfs *
    pub_glfs_gnu340 (const char * volname)
    {
        ...
    }
    GFAPI_SYMVER_PUBLIC(glfs_gnu340, glfs_gnu, 3.4.0);
```

+ create the new definition of glfs_gnu in glfs.c:

```C
    struct glfs *
    pub_glfs_gnu (const char * volname, void *stuff)
    {
        ...
    }
    GFAPI_SYMVER_PUBLIC_DEFAULT(glfs_gnu, 3.7.0);
```

+ Add the new API to the ELF, gnu toolchain link map file, gfapi.map

```
  GFAPI_3.7.0 {
          global:
                  glfs_gnu;
  } GFAPI_PRIVATE_3.7.0;
```

+ Update the OS X alias list file, gfapi.aliases, for both versions:

Change the old line:
```C
  _pub_glfs_gnu _glfs_gnu$GFAPI_3.4.0
```
to:
```C
  _pub_glfs_gnu340 _glfs_gnu$GFAPI_3.4.0
```

Add a new line:
```C
  _pub_glfs_gnu _glfs_gnu$GFAPI_3.7.0
```

+ Lastly, change all gfapi internal calls glfs_gnu to the new API.

