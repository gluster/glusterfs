Previous mechanism:
====================

The only way we could have retrieved the number of files/objects in a directory or volume was to do a crawl of the entire directory/volume. That was expensive and was not scalable.

New Design Implementation:
==========================
The proposed mechanism will provide an easier alternative to determine the count of files/objects in a directory or volume.

The new mechanism will store count of objects/files as part of an extended attribute of a directory. Each directory extended attribute value will indicate the number of files/objects present in a tree with the directory being considered as the root of the tree.

Inode quota management
======================

**setting limits**

Syntax:
*gluster volume quota <volname\> limit-objects <path\> <number\>*

Details:
<number\> is a hard-limit for number of objects limitation for path <path\>. If hard-limit is exceeded, creation of file or directory is no longer permitted.

**list-objects**

Syntax:
*gluster volume quota <volname\> list-objects \[path\] ...*

Details:
If path is not specified, then all the directories which has object limit set on it will be displayed. If we provide path then only that particular path is displayed along with the details associated with that.

Sample output:

       Path                   Hard-limit  Soft-limit     Files       Dirs     Available  Soft-limit exceeded? Hard-limit exceeded?
       ---------------------------------------------------------------------------------------------------------------------------------------------
       /dir                     10           80%           0          1           9               No                   No

**Deleting limits**

Syntax:
*gluster volume quota <volname\> remove-objects <path\>*

Details:
This will remove the object limit set on the specified path.

Note: There is a known issue associated with remove-objects. When both usage limit and object limit is set on a path, then removal of any limit will lead to removal of other limit as well. This is tracked in the bug #1202244


