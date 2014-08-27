#Convert GFID to Path

GlusterFS internal file identifier (GFID) is a uuid that is unique to each
file across the entire cluster. This is analogous to inode number in a
normal filesystem. The GFID of a file is stored in its xattr named
`trusted.gfid`.

####Special mount using [gfid-access translator][1]:
~~~
mount -t glusterfs -o aux-gfid-mount vm1:test /mnt/testvol
~~~

Assuming, you have `GFID` of a file from changelog (or somewhere else).
For trying this out, you can get `GFID` of a file from mountpoint:
~~~
getfattr -n glusterfs.gfid.string /mnt/testvol/dir/file
~~~


---
###Get file path from GFID (Method 1):
**(Lists hardlinks delimited by `:`, returns path as seen from mountpoint)**

####Turn on build-pgfid option
~~~
gluster volume set test build-pgfid on
~~~
Read virtual xattr `glusterfs.ancestry.path` which contains the file path
~~~
getfattr -n glusterfs.ancestry.path -e text /mnt/testvol/.gfid/<GFID>
~~~

**Example:**
~~~
[root@vm1 glusterfs]# ls -il /mnt/testvol/dir/
total 1
10610563327990022372 -rw-r--r--. 2 root root 3 Jul 17 18:05 file
10610563327990022372 -rw-r--r--. 2 root root 3 Jul 17 18:05 file3

[root@vm1 glusterfs]# getfattr -n glusterfs.gfid.string /mnt/testvol/dir/file
getfattr: Removing leading '/' from absolute path names
# file: mnt/testvol/dir/file
glusterfs.gfid.string="11118443-1894-4273-9340-4b212fa1c0e4"

[root@vm1 glusterfs]# getfattr -n glusterfs.ancestry.path -e text /mnt/testvol/.gfid/11118443-1894-4273-9340-4b212fa1c0e4
getfattr: Removing leading '/' from absolute path names
# file: mnt/testvol/.gfid/11118443-1894-4273-9340-4b212fa1c0e4
glusterfs.ancestry.path="/dir/file:/dir/file3"
~~~

---
###Get file path from GFID (Method 2):
**(Does not list all hardlinks, returns backend brick path)**
~~~
getfattr -n trusted.glusterfs.pathinfo -e text /mnt/testvol/.gfid/<GFID>
~~~

**Example:**
~~~
[root@vm1 glusterfs]# getfattr -n trusted.glusterfs.pathinfo -e text /mnt/testvol/.gfid/11118443-1894-4273-9340-4b212fa1c0e4
getfattr: Removing leading '/' from absolute path names
# file: mnt/testvol/.gfid/11118443-1894-4273-9340-4b212fa1c0e4
trusted.glusterfs.pathinfo="(<DISTRIBUTE:test-dht> <POSIX(/mnt/brick-test/b):vm1:/mnt/brick-test/b/dir//file3>)"
~~~

---
###Get file path from GFID (Method 3):
https://gist.github.com/semiosis/4392640

---
####References and links:
[posix: placeholders for GFID to path conversion](http://review.gluster.org/5951)
[1]: https://github.com/gluster/glusterfs/blob/master/doc/features/gfid-access.md
