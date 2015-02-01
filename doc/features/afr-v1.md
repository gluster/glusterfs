#Automatic File Replication
Afr xlator in glusterfs is responsible for replicating the data across the bricks.

###Responsibilities of AFR
Its responsibilities include the following:

1. Maintain replication consistency (i.e. Data on both the bricks should be same, even in the cases where there are operations happening on same file/directory in parallel from multiple applications/mount points as long as all the bricks in replica set are up)

2. Provide a way of recovering data in case of failures as long as there is
  at least one brick which has the correct data.

3. Serve fresh data for read/stat/readdir etc

###Transaction framework
For 1, 2 above afr uses transaction framework which consists of the following 5
phases which happen on all the bricks in replica set(Bricks which are in replication):

####1.Lock Phase
####2. Pre-op Phase
####3. Op Phase
####4. Post-op Phase
####5. Unlock Phase

*Op Phase* is the actual operation sent by application (`write/create/unlink` etc). For every operation which afr receives that modifies data it sends that same operation in parallel to all the bricks in its replica set. This is how it achieves replication.

*Lock, Unlock Phases* take necessary locks so that *Op phase* can provide **replication consistency** in normal work flow.

#####For example:
If an application performs `touch a` and the other one does `mkdir a`, afr makes sure that either file with name `a` or directory with name `a` is created on both the bricks.

*Pre-op, Post-op Phases* provide changelogging which enables afr to figure out which copy is fresh.
Once afr knows how to figure out fresh copy in the replica set it can **recover data** from fresh copy to stale copy. Also it can **serve fresh** data for `read/stat/readdir` etc.

##Internal Operations
Brief introduction to internal operations in Glusterfs which make *Locking, Unlocking, Pre/Post ops* possible:

###Internal Locking Operations
Glusterfs has **locks** translator which provides the following internal locking operations called `inodelk`, `entrylk` which are used by afr to achieve synchronization of operations on files or directories that conflict with each other.

`Inodelk` gives the facility for translators in Glusterfs to obtain range (denoted by tuple with **offset**, **length**) locks in a given domain for an inode.
Full file lock is denoted by the tuple (offset: `0`, length: `0`) i.e. length `0` is considered as infinity.

`Entrylk` enables translators of Glusterfs to obtain locks on `name` in a given domain for an inode, typically a directory.

**Locks** translator provides both *blocking* and *nonblocking* variants and of these operations.

###Xattrop
For pre/post ops posix translator provides an operation called xattrop.
xattrop is a way of *incrementing*/*decrementing* a number present in the extended attribute of the inode *atomically*.

##Transaction Types
There are 3 types of transactions in AFR.
1. Data transactions
  - Operations that add/modify/truncate the file contents.
  - `Write`/`Truncate`/`Ftruncate` etc

2. Metadata transactions
  - Operations that modify the data kept in inode.
  - `Chmod`/`Chown` etc

3) Entry transactions
  - Operations that add/remove/rename entries in a directory
  - `Touch`/`Mkdir`/`Mknod` etc

###Data transactions:

*write* (`offset`, `size`) - writes data from `offset` of `size`

*ftruncate*/*truncate* (`offset`) - truncates data from `offset` till the end of file.

Afr internal locking needs to make sure that two conflicting data operations happen in order, one after the other so that it does not result in replication inconsistency. Afr data operations take inodelks in same domain (lets call it `data` domain).

*Write* operation with offset `O` and size `S` takes an inode lock in data domain with range `(O, S)`.

*Ftruncate*/*Truncate* operations with offset `O` take inode locks in `data` domain with range `(O, 0)`. Please note that size `0` means size infinity.

These ranges make sure that overlapping write/truncate/ftruncate operations are done one after the other.

Now that we know the ranges the operations take locks on, we will see how locking happens in afr.

####Lock:
Afr initially attempts **non-blocking** locks on **all** the bricks of the replica set in **parallel**. If all the locks are successful then it goes on to perform pre-op. But in case **non-blocking** locks **fail** because there is *at least one conflicting operation* which already has a **granted lock** then it **unlocks** the **non-blocking** locks it already acquired in this previous step and proceeds to perform **blocking** locks **one after the other** on each of the subvolumes in the order of subvolumes specified in the volfile.

Chances of **conflicting operations** is **very low** and time elapsed in **non-blocking** locks phase is `Max(latencies of the bricks for responding to inodelk)`, where as time elapsed in **blocking locks** phase is `Sum(latencies of the bricks for responding to inodelk)`. That is why afr always tries for non-blocking locks first and only then it moves to blocking locks.

####Pre-op:
Each file/dir in a brick maintains the changelog(roughly pending operation count) of itself and that of the files
present in all the other bricks in it's replica set as seen by that brick.

Lets consider an example replica volume with 2 bricks brick-a and brick-b.
all files in brick-a will have 2 entries
one for itself and the other for the file present in it's replica set, i.e.brick-b:
One can inspect changelogs using getfattr command.

    # getfattr -d -e hex -m. brick-a/file.txt
    trusted.afr.vol-client-0=0x000000000000000000000000 -->changelog for itself (brick-a)
    trusted.afr.vol-client-1=0x000000000000000000000000 -->changelog for brick-b as seen by brick-a

Likewise, all files in brick-b will have:

    # getfattr -d -e hex -m. brick-b/file.txt
    trusted.afr.vol-client-0=0x000000000000000000000000 -->changelog for brick-a as seen by brick-b
    trusted.afr.vol-client-1=0x000000000000000000000000 -->changelog for itself (brick-b)

#####Interpreting Changelog Value:
Each extended attribute has a value which is `24` hexa decimal digits. i.e. `12` bytes
First `8` digits (`4` bytes) represent changelog of `data`. Second `8` digits represent changelog
of `metadata`. Last 8 digits represent Changelog of `directory entries`.

Pictorially representing the same, we have:

    0x 00000000 00000000 00000000
          |        |        |
          |        |        \_ changelog of directory entries
          |        \_ changelog of metadata
          \ _ changelog of data

Before write operation is performed on the brick, afr marks the file saying there is a pending operation.

As part of this pre-op afr sends xattrop operation with increment 1 for data operation to make the extended attributes the following:
    # getfattr -d -e hex -m. brick-a/file.txt
    trusted.afr.vol-client-0=0x000000010000000000000000 -->changelog for itself (brick-a)
    trusted.afr.vol-client-1=0x000000010000000000000000 -->changelog for brick-b as seen by brick-a

Likewise, all files in brick-b will have:

    # getfattr -d -e hex -m. brick-b/file.txt
    trusted.afr.vol-client-0=0x000000010000000000000000 -->changelog for brick-a as seen by brick-b
    trusted.afr.vol-client-1=0x000000010000000000000000 -->changelog for itself (brick-b)

As the operation is in progress on files on both the bricks all the extended attributes show the same value.

####Op:
Now it sends the actual write operation to both the bricks. Afr remembers whether the operation is successful or not on all the subvolumes.

####Post-Op:
If the operation succeeds on all the bricks then there is no pending operations on any of the bricks so as part of POST-OP afr sends xattrop operation with increment -1 i.e. decrement by 1 for data operation to make the extended attributes back to all zeros again.

In case there is a failure on brick-b then there is still a pending operation on brick-b where as no pending operations are there on brick-a. So xattrop operation for both of these extended attributes differs now. For extended attribute corresponding to brick-a i.e. trusted.afr.vol-client-0 decrement by 1 is sent where as for extended attribute corresponding to brick-b increment by '0' is sent to retain the pending operation count.

    # getfattr -d -e hex -m. brick-a/file.txt
    trusted.afr.vol-client-0=0x000000000000000000000000 -->changelog for itself (brick-a)
    trusted.afr.vol-client-1=0x000000010000000000000000 -->changelog for brick-b as seen by brick-a

    # getfattr -d -e hex -m. brick-b/file.txt
    trusted.afr.vol-client-0=0x000000000000000000000000 -->changelog for brick-a as seen by brick-b
    trusted.afr.vol-client-1=0x000000010000000000000000 -->changelog for itself (brick-b)

####Unlock:
Once the transaction is completed unlock is sent on all the bricks where lock is acquired.


###Meta Data transactions:

setattr, setxattr, removexattr
All metadata operations take same inode lock with same range in metadata domain.

####Lock:
Metadata locking also starts initially with non-blocking locks then move on to blocking locks on any failures because of conflicting operations.

####Pre-op:
Before metadata operation is performed on the brick, afr marks the file saying there is a pending operation.
As part of this pre-op afr sends xattrop operation with increment 1 for metadata operation to make the extended attributes the following:
    # getfattr -d -e hex -m. brick-a/file.txt
    trusted.afr.vol-client-0=0x000000000000000100000000 -->changelog for itself (brick-a)
    trusted.afr.vol-client-1=0x000000000000000100000000 -->changelog for brick-b as seen by brick-a

Likewise, all files in brick-b will have:
    # getfattr -d -e hex -m. brick-b/file.txt
    trusted.afr.vol-client-0=0x000000000000000100000000 -->changelog for brick-a as seen by brick-b
    trusted.afr.vol-client-1=0x000000000000000100000000 -->changelog for itself (brick-b)

As the operation is in progress on files on both the bricks all the extended attributes show the same value.

####Op:
Now it sends the actual metadata operation to both the bricks. Afr remembers whether the operation is successful or not on all the subvolumes.

Post-Op:
If the operation succeeds on all the bricks then there is no pending operations on any of the bricks so as part of POST-OP afr sends xattrop operation with increment -1 i.e. decrement by 1 for metadata operation to make the extended attributes back to all zeros again.

In case there is a failure on brick-b then there is still a pending operation on brick-b where as no pending operations are there on brick-a. So xattrop operation for both of these extended attributes differs now. For extended attribute corresponding to brick-a i.e. trusted.afr.vol-client-0 decrement by 1 is sent where as for extended attribute corresponding to brick-b increment by '0' is sent to retain the pending operation count.

    # getfattr -d -e hex -m. brick-a/file.txt
    trusted.afr.vol-client-0=0x000000000000000000000000 -->changelog for itself (brick-a)
    trusted.afr.vol-client-1=0x000000000000000100000000 -->changelog for brick-b as seen by brick-a

    # getfattr -d -e hex -m. brick-b/file.txt
    trusted.afr.vol-client-0=0x000000000000000000000000 -->changelog for brick-a as seen by brick-b
    trusted.afr.vol-client-1=0x000000000000000100000000 -->changelog for itself (brick-b)

####Unlock:
Once the transaction is completed unlock is sent on all the bricks where lock is acquired.


###Entry transactions:

create, mknod, mkdir, link, symlink, rename, unlink, rmdir
Pre-op/Post-op (done using xattrop) always happens on the parent directory.

Entry Locks taken by these entry operations:

**Create** (file `dir/a`): Lock on name `a` in inode of `dir`

**mknod** (file `dir/a`): Lock on name `a` in inode of `dir`

**mkdir** (dir `dir/a`): Lock on name `a` in inode of `dir`

**link** (file `oldfile`, file `dir/newfile`): Lock on name `newfile` in inode of `dir`

**Symlink** (file `oldfile`, file `dir`/`symlinkfile`): Lock on name `symlinkfile` in inode of `dir`

**rename** of (file `dir1`/`file1`, file `dir2`/`file2`): Lock on name `file1` in inode of `dir1`, Lock on name `file2` in inode of `dir2`

**rename** of (dir `dir1`/`dir2`, dir `dir3`/`dir4`): Lock on name `dir2` in inode of `dir1`, Lock on name `dir4` in inode of `dir3`, Lock on `NULL` in inode of `dir4`

**unlink** (file `dir`/`a`): Lock on name `a` in inode of `dir`

**rmdir** (dir dir/a): Lock on name `a` in inode of `dir`, Lock on `NULL` in inode of `a`

####Lock:
Even entry locking starts initially with non-blocking locks then move on to blocking locks on any failures because of conflicting operations.

####Pre-op:
Before entry operation is performed on the brick, afr marks the directory saying there is a pending operation.

As part of this pre-op afr sends xattrop operation with increment 1 for entry operation to make the extended attributes the following:

    # getfattr -d -e hex -m. brick-a/
    trusted.afr.vol-client-0=0x000000000000000000000001 -->changelog for itself (brick-a)
    trusted.afr.vol-client-1=0x000000000000000000000001 -->changelog for brick-b as seen by brick-a

Likewise, all files in brick-b will have:
    # getfattr -d -e hex -m. brick-b/
    trusted.afr.vol-client-0=0x000000000000000000000001 -->changelog for brick-a as seen by brick-b
    trusted.afr.vol-client-1=0x000000000000000000000001 -->changelog for itself (brick-b)

As the operation is in progress on files on both the bricks all the extended attributes show the same value.

####Op:
Now it sends the actual entry operation to both the bricks. Afr remembers whether the operation is successful or not on all the subvolumes.

####Post-Op:
If the operation succeeds on all the bricks then there is no pending operations on any of the bricks so as part of POST-OP afr sends xattrop operation with increment -1 i.e. decrement by 1 for entry operation to make the extended attributes back to all zeros again.

In case there is a failure on brick-b then there is still a pending operation on brick-b where as no pending operations are there on brick-a. So xattrop operation for both of these extended attributes differs now. For extended attribute corresponding to brick-a i.e. trusted.afr.vol-client-0 decrement by 1 is sent where as for extended attribute corresponding to brick-b increment by '0' is sent to retain the pending operation count.

    # getfattr -d -e hex -m. brick-a/file.txt
    trusted.afr.vol-client-0=0x000000000000000000000000 -->changelog for itself (brick-a)
    trusted.afr.vol-client-1=0x000000000000000000000001 -->changelog for brick-b as seen by brick-a

    # getfattr -d -e hex -m. brick-b/file.txt
    trusted.afr.vol-client-0=0x000000000000000000000000 -->changelog for brick-a as seen by brick-b
    trusted.afr.vol-client-1=0x000000000000000000000001 -->changelog for itself (brick-b)

####Unlock:
Once the transaction is completed unlock is sent on all the bricks where lock is acquired.

The parts above cover how replication consistency is achieved in afr.

Now let us look at how afr can figure out how to recover from failures given the changelog extended attributes

###Recovering from failures (Self-heal)
For recovering from failures afr tries to determine which copy is the fresh copy based on the extended attributes.

There are 3 possibilities:
1. All the extended attributes are zero on all the bricks. This means there are no pending operations on any of the bricks so there is nothing to recover.
2. According to the extended attributes there is a brick(brick-a) which noticed that there are operations pending on the other brick(brick-b).
 - There are 4 possibilities for brick-b

    - It did not even participate in transaction (all extended attributes on brick-b are zeros). Choose brick-a as source and perform recovery to brick-b.

    - It participated in the transaction but died even before post-op. (All extended attributes on brick-b have a pending-count). Choose brick-a as source and perform recovery to brick-b.

    - It participated in the transaction and after the post-op extended attributes on brick-b show that there are pending operations on itself. Choose brick-a as source and perform recovery to brick-b.

    - It participated in the transaction and after the post-op extended attributes on brick-b show that there are pending operations on brick-a. This situation is called Split-brain and there is no way to recover. This situation can happen in cases of network partition.

3. The only possibility now is where both brick-a, brick-b have pending operations. In this case changelogs extended attributes are all non-zeros on all the bricks. Basically what could have happened is the operations started on the file but either the whole replica set went down or the mount process itself dies before post-op is performed. In this case there is a possibility that data on the bricks is different. In this case afr chooses file with bigger size as source, if both files have same size then it choses the subvolume which has witnessed large number of pending operations on the other brick as source. If both have same number of pending operations then it chooses the file with newest ctime as source. If this is also same then it just picks one of the two bricks as source and syncs data on to the other to make sure that the files are replicas to each other.

###Self-healing:
Afr does 3 types of self-heals for data recovery.

1. Data self-heal

2. Metadata self-heal

3. Entry self-heal

As we have seen earlier, afr depends on changelog extended attributes to figure out which copy is source and which copy is sink. General algorithm for performing this recovery (self-heal) is same for all of these different self-heals.

1. Take appropriate full locks on the file/directory to make sure no other transaction is in progress while inspecting changelog extended attributes.
In this step, for
 - Data self-heal afr takes inode lock with `offset: 0` and `size: 0`(infinity) in data domain.
 - Entry self-heal takes entry lock on directory with `NULL` name i.e. full directory lock.
 - Metadata self-heal it takes pre-defined range in metadata domain on which all the metadata operations on that inode take locks on. To prevent duplicate data self-heal an inode lock is taken in self-heal domain as well.

2. Perform Sync from fresh copy to stale copy.
In this step,
 - Metadata self-heal gets the inode attributes, extended attributes from source copy and sets them on the stale copy.

 - Entry self-heal reads entries on stale directories and see if they are present on source directory, if they are not present it deletes them. Then it reads entries on fresh directory and creates the missing entries on stale directories.

 - Data self-heal does things a bit differently to make sure no other writes on the file are blocked for the duration of self-heal because files sizes could be as big as 100G(VM files) and we don't want to block all the transactions until the self-heal is over. Locks translator allows two overlapping locks to be granted if they are from same lock owner. Using this what data self-heal does is it takes a small 128k size range lock and unlock previous acquired lock, heals just that 128k chunk and takes next 128k chunk lock and unlock previous lock and moves to the next one. It always makes sure that at least one lock is present on the file by selfheal throughout the duration of self-heal so that two self-heals don't happen in parallel.

 - Data self-heal has two algorithms, where the file can be copied only when there is data mismatch for that chunk called as 'diff' self-heal. The otherone is blind copy of each chunk called 'full' self-heal

3. Change extended attributes to mark new sources after the sync.

4. Unlock the locks acquired to perform self-heal.

### Transaction Optimizations:
As we saw earlier afr transaction for all the operations that modify data happens in 5 phases, i.e. it sends 5 operations on the network for every operation. In the following sections we will see optimizations already implemented in afr which reduce the number of operations on the network to just 1 per transaction in best case.

####Changelog-piggybacking
This optimization comes into picture when on same file descriptor, before write1's post op is complete write2's pre-op starts and the operations are succeeding. When writes come in that manner we can piggyback on the pre-op of write1 for write2 and somehow tell write1 that write2 will do the post-op that was supposed to be done by write1. So write1's post-op does not happen over network, write2's pre-op does not happen over network. This optimization does not hold if there are any failures in write1's phases.

####Delayed Post-op
This optimization just delays post-op of the write transaction(write1) by a pre-configured amount time to increase the probability of next write piggybacking on the pre-op done by write1.

With the combination of these two optimizations for operations like full file copy which are write intensive operations, what will essentially happen is for the first write a pre-op will happen. Then for the last write on the file post-op happens. So for all the write transactions between first write and last write afr reduced network operations from 5 to 3.

####Eager-locking:
This optimization comes into picture when only one file descriptor is open on the file and performing writes just like in the previous optimization. What this optimization does is it takes a full file lock on the file irrespective of the offset, size of the write, so that lock acquired by write1 can be piggybacked by write2 and write2 takes the responsibility of unlocking it. both write1, write2 will have same lock owner and afr takes the responsibility of serializing overlapping writes so that replication consistency is maintained.

With the combination of these optimizations for operations like full file copy which are write intensive operations, what will essentially happen is for the first write a pre-op, full-file lock will happen. Then for the last write on the file post-op, unlock happens. So for all the write transactions between first write and last write afr reduced network operations from 5 to 1.

###Quorum in afr:
To avoid split-brains, afr employs the following quorum policies.
 - In replica set with odd number of bricks, replica set is said to be in quorum if more than half of the bricks are up.
 - In replica set with even number of bricks, if more than half of the bricks are up then it is said to be in quorum but if number of bricks that are up is equal to number of bricks that are down then, it is said to be in quorum if the first brick is also up in the set of bricks that are up.

When quorum is not met in the replica set then modify operations on the mount are not allowed by afr.

###Self-heal daemon and Index translator usage by afr:

####Index xlator:
On each brick index xlator is loaded. This xlator keeps track of what is happening in afr's pre-op and post-op. If there is an ongoing I/O or a pending self-heal, changelog xattrs would have non-zero values. Whenever xattrop/fxattrop fop (pre-op, post-ops are done using these fops) comes to index xlator a link (with gfid as name of the file on which the fop is performed) is added in <brick>/.glusterfs/indices/xattrop directory. If the value returned by the fop is zero the link is removed from the index otherwise it is kept until zero is returned in the subsequent xattrop/fxattrop fops.

####Self-heal-daemon:
self-heal-daemon process keeps running on each machine of the trusted storage pool. This process has afr xlators of all the volumes which are started. Its job is to crawl indices on bricks that are local to that machine. If any of the files represented by the gfid of the link name need healing and automatically heal them. This operation is performed every 10 minutes for each replica set. Additionally when a brick comes online also this operation is performed.
