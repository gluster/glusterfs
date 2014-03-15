cluster/afr translator
======================

Locking
-------

Before understanding replicate, one must understand two internal FOPs:

### `GF_FILE_LK`

This is exactly like `fcntl(2)` locking, except the locks are in a
separate domain from locks held by applications.

### `GF_DIR_LK (loc_t *loc, char *basename)`

This allows one to lock a name under a directory. For example,
to lock /mnt/glusterfs/foo, one would use the call:

```
GF_DIR_LK ({loc_t for "/mnt/glusterfs"}, "foo")
```

If one wishes to lock *all* the names under a particular directory,
supply the basename argument as `NULL`.

The locks can either be read locks or write locks; consult the
function prototype for more details.

Both these operations are implemented by the features/locks (earlier
known as posix-locks) translator.

Basic design
------------

All FOPs can be classified into four major groups:

### inode-read

Operations that read an inode's data (file contents) or metadata (perms, etc.).

access, getxattr, fstat, readlink, readv, stat.

### inode-write

Operations that modify an inode's data or metadata.

chmod, chown, truncate, writev, utimens.

### dir-read

Operations that read a directory's contents or metadata.

readdir, getdents, checksum.

### dir-write

Operations that modify a directory's contents or metadata.

create, link, mkdir, mknod, rename, rmdir, symlink, unlink.

Some of these make a subgroup in that they modify *two* different entries:
link, rename, symlink.

### Others

Other operations.

flush, lookup, open, opendir, statfs.

Algorithms
----------

Each of the four major groups has its own algorithm:

### inode-read, dir-read

1. Send a request to the first child that is up:
    * if it fails:
        * try the next available child
    * if we have exhausted all children:
        * return failure

### inode-write

 All operations are done in parallel unless specified otherwise.

1. Send a ``GF_FILE_LK`` request on all children for a write lock on the 
   appropriate region
   (for metadata operations: entire file (0, 0) for writev:
   (offset, offset+size of buffer))
    * If a lock request fails on a child:
        * unlock all children
        * try to acquire a blocking lock (`F_SETLKW`) on each child, serially.
	  If this fails (due to `ENOTCONN` or `EINVAL`):
          Consider this child as dead for rest of transaction.
2. Mark all children as "pending" on all (alive) children (see below for 
meaning of "pending").
    * If it fails on any child:
        * mark it as dead (in transaction local state).
3. Perform operation on all (alive) children.
    * If it fails on any child:
        * mark it as dead (in transaction local state).
4. Unmark all successful children as not "pending" on all nodes.
5. Unlock region on all (alive) children.

### dir-write

 The algorithm for dir-write is same as above except instead of holding
 `GF_FILE_LK` locks we hold a GF_DIR_LK lock on the name being operated upon.
 In case of link-type calls, we hold locks on both the operand names.

"pending"
---------

The "pending" number is like a journal entry. A pending entry is an
array of 32-bit integers stored in network byte-order as the extended
attribute of an inode (which can be a directory as well).

There are three keys corresponding to three types of pending operations:

### `AFR_METADATA_PENDING`

There are some metadata operations pending on this inode (perms, ctime/mtime,
xattr, etc.).

### `AFR_DATA_PENDING`

There is some data pending on this inode (writev).

### `AFR_ENTRY_PENDING`

There are some directory operations pending on this directory
(create, unlink, etc.).

Self heal
---------

* On lookup, gather extended attribute data:
    * If entry is a regular file:
        * If an entry is present on one child and not on others:
            * create entry on others.
        * If entries exist but have different metadata (perms, etc.):
            * consider the entry with the highest `AFR_METADATA_PENDING` number as
              definitive and replicate its attributes on children.
    * If entry is a directory:
        * Consider the entry with the highest `AFR_ENTRY_PENDING` number as
          definitive and replicate its contents on all children.
    * If any two entries have non-matching types (i.e., one is file and
      other is directory):
        * Announce to the user via log that a split-brain situation has been
          detected, and do nothing.
* On open, gather extended attribute data:
    * Consider the file with the highest `AFR_DATA_PENDING` number as
      the definitive one and replicate its contents on all other
      children.

During all self heal operations, appropriate locks must be held on all
regions/entries being affected.

Inode scaling
-------------

Inode scaling is necessary because if a situation arises where an inode number 
is returned for a directory (by lookup) which was previously the inode number 
of a file (as per FUSE's table), then FUSE gets horribly confused (consult a 
FUSE expert for more details).

To avoid such a situation, we distribute the 64-bit inode space equally
among all children of replicate.

To illustrate:

If c1, c2, c3 are children of replicate, they each get 1/3 of the available
inode space:

-------------  --  --  --  --  --  --  --  --  --  --  --  ---
Child:         c1  c2  c3  c1  c2  c3  c1  c2  c3  c1  c2  ...
Inode number:  1    2   3   4   5   6   7   8   9  10  11  ...
-------------  --  --  --  --  --  --  --  --  --  --  --  ---

Thus, if lookup on c1 returns an inode number "2", it is scaled to "4"
(which is the second inode number in c1's space).

This way we ensure that there is never a collision of inode numbers from
two different children.

This reduction of inode space doesn't really reduce the usability of
replicate since even if we assume replicate has 1024 children (which would be a
highly unusual scenario), each child still has a 54-bit inode space:
$2^{54} \sim 1.8 \times 10^{16}$, which is much larger than any real
world requirement.
