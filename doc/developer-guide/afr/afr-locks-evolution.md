History of locking in AFR
--------------------------

GlusterFS has **locks** translator which provides the following internal locking operations called `inodelk`, `entrylk` which are used by afr to achieve synchronization of operations on files or directories that conflict with each other.

`Inodelk` gives the facility for translators in GlusterFS to obtain range (denoted by tuple with **offset**, **length**) locks in a given **domain** for an inode.
Full file lock is denoted by the tuple (offset: `0`, length: `0`) i.e. length `0` is considered as infinity.

`Entrylk` enables translators of GlusterFS to obtain locks on `name` in a given **domain** for an inode, typically a directory.

The **locks** translator provides both *blocking* and *nonblocking* variants and of these locks.


AFR makes use of locks xlator extensively:

1)For FOPS (from clients)
-----------------------
* Data transactions take inode locks on data domain, Let's refer to this domain name as DATA_DOMAIN.

  So locking for writes would be something like this:`inodelk(offset,length, DATA_DOMAIN)`
  For truncating a file to zero, it would be `inodelk(0,0,DATA_DOMAIN)`

* Metadata transactions (chown/chmod) also take inode locks but on a special range on metadata domain,
  i.e.`(LLONG_MAX-1 , 0, METADATA_DOMAIN).`

* Entry transactions (create, mkdir, rmdir,unlink, symlink, link,rename) take entrylk on `(name, parent inode)`.


2)For self heal:
-------------
* For Metadata self-heal, it is the same.  i.e.`inodelk(LLONG_MAX-1 , 0, METADATA_DOMAIN)`.
* For Entry self-heal, it is `entrylk(NULL name, parent inode)`. Specifying NULL for the name takes full lock on the directory referred to by the inode.
* For data self-heal, there is a bit of history as to how locks evolved:

###Initial version (say version 1) :
There was no concept of selfheal daemon (shd). Only client lookups triggered heals. so AFR always took `inodelk(0,0,DATA_DOMAIN)` for healing. The issue with this approach was that when heal was in progress, I/O from clients was blocked .

###version 2:
shd was introduced. We needed to allow I/O to go through when heal was going,provided the ranges did not overlap. To that extent, the following approach was adopted:

+ 1.shd takes (full inodelk in DATA_DOMAIN). Thus client FOPS are blocked and cannot modify changelog-xattrs
+ 2.shd inspects xattrs to determine source/sink
+ 3.shd takes a chunk inodelk(0-128kb) again in DATA_DOMAIN (locks xlator allows overlapping locks if lock owner is the same).
+ 4.unlock full lock
+ 5.heal
+ 6.take next chunk lock(129-256kb)
+ 7.unlock 1st chunk lock, heal the second chunk and so on.


Thus after 4, any client FOP could write to regions that was not currently under heal. The exception was truncate (to size 0) because it needs full file lock and will always block because some chunk is always under lock by the shd until heal completes.

Another issue was that 2 shds could run in parallel. Say SHD1 and SHD2 compete for step 1. Let SHD1 win. It proceeds and completes step 4. Now SHD2 also succeeds in step 1, continues all steps. Thus at the end both shds will decrement the changelog leading to negative values in it)  

### version 3
To prevent parallel self heals, another domain was introduced, let us call it SELF_HEAL_DOMAIN. With this domain, the following approach was adopted and is **the approach currently in use**:

+ 1.shd takes (full inodelk on SELF_HEAL_DOMAIN)
+ 2.shd takes (full inodelk on DATA_DOMAIN)
+ 3.shd inspects xattrs  to determine source/sink
+ 4.unlock full lock on DATA_DOMAIN
+ 5.take chunk lock(0-128kb) on DATA_DOMAIN
+ 6.heal
+ 7.take next chunk lock(129-256kb) on DATA_DOMAIN
+ 8.unlock 1st chunk lock, heal and so on.
+ 9.Finally release full lock on SELF_HEAL_DOMAIN

Thus until one shd completes step 9, another shd cannot start step 1, solving the problem of simultaneous heals.
Note that the issue of truncate (to zero) FOP hanging still remains.
Also there are multiple network calls involved in this scheme. (lock,heal(ie read+write), unlock) per chunk. i.e 4 calls per chunk.

### version 4 (ToDo)
Some improvements that need to be made in version 3:
* Reduce network calls using piggy backing.
* After taking chunk lock and healing, we need to unlock the lock before locking the next chunk. This gives a window for any pending truncate FOPs to succeed. If truncate succeeds, the heal of next chunk will fail (read returns zero)
and heal is stopped. *BUT* there is **yet another** issue:

* shd does steps 1 to 4. Let's assume source is brick b1, sink is brick b2 . i.e xattrs are (0,1) and (0,0) on b1 and b2 respectively. Now before shd takes (0-128kb) lock, a client FOP takes it.
It modifies data but the FOP succeeds only on brick 2. writev returns success, and the attrs now read (0,1) (1,0). SHD takes over and heals. It had observed (0,1),(0,0) earlier
and thus goes ahead and copies stale 128Kb from brick 1 to brick2. Thus as far as application is concerned, `writev` returned success but bricks have stale data.
What needs to be done is `writev` must return success only if it succeeded on atleast one source brick (brick b1 in this case). Otherwise  The heal still happens in reverse direction but as far as the application is concerned, it received an error.  

###Note on lock **domains**
We have used conceptual names in this document like DATA_DOMAIN/ METADATA_DOMAIN/ SELF_HEAL_DOMAIN. In the code, these are mapped to strings that are based on the AFR xlator name like so:

DATA_DOMAIN     --->"vol_name-replicate-n"

METADATA_DOMAIN  --->"vol_name-replicate-n:metadata"

SELF_HEAL_DOMAIN -->"vol_name-replicate-n:self-heal"

where vol_name is the name of the volume and 'n' is the replica subvolume index (starting from 0).
