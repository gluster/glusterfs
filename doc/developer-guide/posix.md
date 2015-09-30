storage/posix translator
========================

Notes
-----

### `SET_FS_ID`

This is so that all filesystem checks are done with the user's
uid/gid and not GlusterFS's uid/gid.

### `MAKE_REAL_PATH`

This macro concatenates the base directory of the posix volume
('option directory') with the given path.

### `need_xattr` in lookup

If this flag is passed, lookup returns a xattr dictionary that contains
the file's create time, the file's contents, and the version number
of the file.

This is a hack to increase small file performance. If an application
wants to read a small file, it can finish its job with just a lookup
call instead of a lookup followed by read.

### `getdents`/`setdents`

These are used by unify to set and get directory entries.

### `ALIGN_BUF`

Macro to align an address to a page boundary (4K).

### `priv->export_statfs`

In some cases, two exported volumes may reside on the same
partition on the server. Sending statvfs info for both
the volumes will lead to erroneous df output at the client,
since free space on the partition will be counted twice.

In such cases, user can disable exporting statvfs info
on one of the volumes by setting this option.

### `xattrop`

This fop is used by replicate to set version numbers on files.

### `getxattr`/`setxattr` hack to read/write files

A key, `GLUSTERFS_FILE_CONTENT_STRING`, is handled in a special way by
`getxattr`/`setxattr`. A getxattr with the key will return the entire
content of the file as the value. A `setxattr` with the key will write
the value as the entire content of the file.

### `posix_checksum`

This calculates a simple XOR checksum on all entry names in a
directory that is used by unify to compare directory contents.
