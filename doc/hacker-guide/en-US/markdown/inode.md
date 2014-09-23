#Inode and dentry management in GlusterFS:

##Background
Filesystems internally refer to files and directories via inodes. Inodes
are unique identifiers of the entities stored in a filesystem. Whenever an
application has to operate on a file/directory (read/modify), the filesystem
maps that file/directory to the right inode and start referring to that inode
whenever an operation has to be performed on the file/directory.

In GlusterFS a new inode gets created whenever a new file/directory is created
OR when a successful lookup is done on a file/directory for the first time.
Inodes in GlusterFS are maintained by the inode table which gets initiated when
the filesystem daemon is started (both for the brick process as well as the
mount process). Below are some important data structures for inode management.

## Data-structure (inode-table)
```
struct _inode_table {
        pthread_mutex_t    lock;
        size_t             hashsize;    /* bucket size of inode hash and dentry hash */
        char              *name;        /* name of the inode table, just for gf_log() */
        inode_t           *root;        /* root directory inode, with inode
        number and gfid 1 */
        xlator_t          *xl;          /* xlator to be called to do purge and
        the xlator which maintains the inode table*/
        uint32_t           lru_limit;   /* maximum LRU cache size */
        struct list_head  *inode_hash;  /* buckets for inode hash table */
        struct list_head  *name_hash;   /* buckets for dentry hash table */
        struct list_head   active;      /* list of inodes currently active (in an fop) */
        uint32_t           active_size; /* count of inodes in active list */
        struct list_head   lru;         /* list of inodes recently used.
                                           lru.next most recent */
        uint32_t           lru_size;    /* count of inodes in lru list  */
        struct list_head   purge;       /* list of inodes to be purged soon */
        uint32_t           purge_size;  /* count of inodes in purge list */

        struct mem_pool   *inode_pool;  /* memory pool for inodes */
        struct mem_pool   *dentry_pool; /* memory pool for dentrys */
        struct mem_pool   *fd_mem_pool; /* memory pool for fd_t */
        int                ctxcount;    /* number of slots in inode->ctx */
};
```

#Life-cycle
```

inode_table_new (size_t lru_limit, xlator_t *xl)

This is a function which allocates a new inode table. Usually the top xlators in
the graph such as protocol/server (for bricks), fuse and nfs (for fuse and nfs
mounts) and libgfapi do inode managements. Hence they are the ones which will
allocate a new inode table by calling the above function.

Each xlator graph in glusterfs maintains an inode table. So in fuse clients,
whenever there is a graph change due to add brick/remove brick or
addition/removal of some other xlators, a new graph is created which creates a
new inode table.

Thus an allocated inode table is destroyed only when the filesystem daemon is
killed or unmounted.

```

#what it contains.
```

Inode table in glusterfs mainly contains a hash table for maintaining inodes.
In general a file/directory is considered to be existing if there is a
corresponding inode present in the inode table. If a inode for a file/directory
cannot be found in the inode table, glusterfs tries to resolve it by sending a
lookup on the entry for which the inode is needed. If lookup is successful, then
a new inode correponding to the entry is added to the hash table present in the
inode table. Thus an inode present in the hash-table means, its an existing
file/directory within the filesystem. The inode table also contains the hash
size of the hash table (as of now it is hard coded to 14057. The hash value of
a inode is calculated using its gfid).

Apart from the hash table, inode table also maintains 3 important list of inodes
1) Active list:
Active list contains all the active inodes (i.e inodes which are currently part
of some fop).
2) Lru list:
Least recently used inodes list. A limit can be set for the size of the lru
list. For bricks it is 16384 and for clients it is infinity.
3) Purge list:
List of all the inodes which have to be purged (i.e inodes which have to be
deleted from the inode table due to unlink/rmdir/forget).

And at last it also contains the mem-pool for allocating inodes, dentries so
that frequent malloc/calloc and free of the data structures can be avoided.
```

#Data structure (inode)
```
struct _inode {
        inode_table_t       *table;         /* the table this inode belongs to */
        uuid_t               gfid;          /* unique identifier of the inode */
        gf_lock_t            lock;
        uint64_t             nlookup;
        uint32_t             fd_count;      /* Open fd count */
        uint32_t             ref;           /* reference count on this inode */
        ia_type_t            ia_type;       /* what kind of file */
        struct list_head     fd_list;       /* list of open files on this inode */
        struct list_head     dentry_list;   /* list of directory entries for this inode */
        struct list_head     hash;          /* hash table pointers */
        struct list_head     list;          /* active/lru/purge */

        struct _inode_ctx   *_ctx;          /* place holder for keeping the
        information about the inode by different xlators */
};

As said above, inodes are internal way of identifying the files/directories. A
inode uniquely represents a file/directory. A new inode is created whenever a
create/mkdir/symlink/mknod operations are performed. Apart from that a new inode
is created upon the successful fresh lookup of a file/directory. Say the
filesystem contained some file "a" within root and the filesystem was
unmounted. Now when glusterfs is mounted and some operation is perfomed on "/a",
glusterfs tries to get the inode for the entry "a" with parent inode as
root. But, since glusterfs just came up, it will not be able to find the inode
for "a" and will send a lookup on "/a". If the lookup operation succeeds (i.e.
the root of glusterfs contains an entry called "a"), then a new inode for "/a"
is created and added to the inode table.

Depending upon the situation, an inode can be in one of the 3 lists maintained
by the inode table. If some fop is happening on the inode, then the inode will
be present in the active inodes list maintained by the inode table. Active
inodes are those inodes whose refcount is greater than zero. Whenever some
operation comes on a file/directory, and the resolver tries to find the inode
for it, it increments the refcount of the inode before returning the inode. The
refcount of an inode can be incremented by calling the below function

inode_ref (inode_t *inode)

Any xlator which wants to operate on a inode as part of some fop (or wants the
inode in the callback), should hold a ref on the inode.
Once the fop is completed before sending the reply of the fop to the above
layers , the inode has to be unrefed. When the refcount of an inode becomes
zero, it is removed from the active inodes list and put into LRU list maintained
by the inode table. Thus in short if some fop is happening on a file/directory,
the corresponding inode will be in the active list or it will be in the LRU
list.
```

#Life Cycle

A new inode is created whenever a new file/directory/symlink is created OR a
successful lookup of an existing entry is done. The xlators which does inode
management (as of now protocol/server, fuse, nfs, gfapi) will perform inode_link
operation upon successful lookup or successful creation of a new entry.

inode_link (inode_t *inode, inode_t *parent, const char *name,
            struct iatt *buf);

inode_link actually adds the inode to the inode table (to be precise it adds
the inode to the hash table maintained by the inode table. The hash value is
calculated based on the gfid). Copies the gfid to the inode (the gfid is
present in the iatt structure). Creates a dentry with the new name.

A inode is removed from the inode table and eventually destroyed when unlink
or rmdir operation is performed on a file/directory, or the the lru limit of
the inode table has been exceeded.

#Data structure (dentry)
```

struct _dentry {
        struct list_head   inode_list;   /* list of dentries of inode */
        struct list_head   hash;         /* hash table pointers */
        inode_t           *inode;        /* inode of this directory entry */
        char              *name;         /* name of the directory entry */
        inode_t           *parent;       /* directory of the entry */
};

A dentry is the presence of an entry for a file/directory within its parent
directory. A dentry usually points to the inode to which it belongs to. In
glusterfs a dentry contains the following fields.
1) a hook using which it can add itself to the list of
the dentries maintained by the inode to which it points to.
2) A hash table pointer.
3) Pointer to the inode to which it belongs to.
4) Name of the dentry
5) Pointer to the inode of the parent directory in which the dentry is present

A new dentry is created when a new file/directory/symlink is created or a hard
link to an existing file is created.

__dentry_create (inode_t *inode, inode_t *parent, const char *name);

A dentry holds a refcount on the parent
directory so that the parent inode is never removed from the active inode's list
and put to the lru list (If the lru limit of the lru list is exceeded, there is
a chance of parent inode being destroyed. To avoid it, the dentries hold a
reference to the parent inode). A dentry is removed whenevern a unlink/rmdir
is perfomed on a file/directory. Or when the lru limit has been exceeded, the
oldest inodes are purged out of the inode table, during which all the dentries
of the inode are removed.

Whenever a unlink/rmdir comes on a file/directory, the corresponding inode
should be removed from the inode table. So upon unlink/rmdir, the inode will
be moved to the purge list maintained by the inode table and from there it is
destroyed. To be more specific, if a inode has to be destroyed, its refcount
and nlookup count both should become 0. For refcount to become 0, the inode
should not be part of any fop (there should not be any open fds). Or if the
inode belongs to a directory, then there should not be any fop happening on the
directory and it should not contain any dentries within it. For nlookup count to
become zero, a forget has to be sent on the inode with nlookup count set to 0 as
an argument. For fuse clients, forget is sent by the kernel itself whenever a
unlink/rmdir is performed. But for brick processes, upon unlink/rmdir, the
protocol/server itself has to do inode_forget. Whenever the inode has to be
deleted due to file removal or lru limit being exceeded  the inode is retired
(i.e. all the dentries of the inode are deleted and the inode is moved to the
purge list maintained by the inode table), the nlookup count is set to 0 via
inode_forget api. The inode table, then prunes all the inodes from the purge
list by destroying the inode contexts maintained by each xlator.

unlinking of the dentry is done via inode_unlink;

void
inode_unlink (inode_t *inode, inode_t *parent, const char *name);

If the inode has multiple hard links, then the unlink operation performed by
the application results just in the removal of the dentry with the name provided
by the application. For the inode to be removed, all the dentries of the inode
should be unlinked.
```

