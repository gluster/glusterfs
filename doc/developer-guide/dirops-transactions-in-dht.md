Need for transactions during operations on directories arise from two
basic design elements of DHT:

 1. A directory is created on all subvolumes of dht. Since glusterfs
 associates each file-system object with an unique gfid, every
 subvolume should have the same unique mapping of (path of directory,
 gfid). To elaborate,
    * Each subvolume should've same gfid associated with a path to
 directory.
    * A gfid should not be associated with more than one path in any
 subvolume.

 So, entire operations like mkdir, renamedir, rmdir and creation of
 directories during self-heal need to be atomic in dht. In other words,
 any of these operations shouldn't begin on an inode if one of them is
 already in progress on the same inode, till it completes on all
 subvolumes of dht. If not, more than one of these operations
 happening in parallel can break any or all of the two requirements
 listed above. This is referred in the rest of the document by the
 name _Atomicity during namespace operations_.
   
 2. Each directory has an independent layout persisted on
 subvolumes. Each subvolume contains only part of the layout relevant
 to it. For performance reasons _and_ since _only_ dht has aggregated
 view, this layout is cached in memory of client. To make sure dht
 reads or modifies a single complete layout while parallel modifications of    the layout are in progress, we need atomicity during layout modification and reading. This is referred in the rest of the document as  _Atomicity during layout modification and reading_.

Rest of the document explains how atomicity is achieved for each of
the case above.

**Atomicity during layout modification and reading**
File operations a.k.a fops can be classified into two categories based on how they consume layout.

 - Layout writer. Setting of layout during selfheal of a directory is
 layout writer of _that_ directory. 
 - Layout reader. 
     * Any entry fop like create, unlink, rename, link, symlink,
 unlink, mknod, rename, mkdir, rmdir, renamedir which needs layout of the parent directory. Each of these fops are readers of layout on parent directory.
     *  setting of layout during mkdir of a directory is considered as
 a reader of the same directory's layout. The reason for this is that
 only a parallel lookup on that directory can be a competing fop that modifies the layout (Other fops need gfid of the directory which can be got only after either lookup or mkdir completes). However, healing of layout is considered as a writer and a single writer blocks all readers.
 
*Algorithm*
Atomicity is achieved by locking on the inode of directory whose
layout is being modified or read. The fop used is inodelk.
 -  Writer acquires blocking inodelk (directory-inode, write-lock) on
 all subvolumes serially. The order of subvols in which they are
 locked by different clients remains constant for a directory. If locking fails on any subvolume, layout modification is abandoned.
 - Reader acquires an inodelk (directory-inode, read-lock) on _any_
 one subvolume. If locking fails on a subvolume (say with
 ESTALE/ENOTCONN error), locking can be tried on other subvolumes till
 we get one lock. If we cannot get lock on at least one subvolume,
 consistency of layout is not guaranteed. Based on the consistency
 requirements of fops, they can be failed or continued.

Reasons why writer has to lock on _all_ subvols:

 - DHT don't have a metadata server and locking is implemented by brick. So, there is no well-defined subvol/brick that can be used as an arbitrator by different clients while acquiring locks.
 - readers should acquire as minimum number of locks as possible. In
 other words, the algorithm aims to have less synchronization cost to
 readers.
 - The subvolume to which a directory hashes could be used as a
 lock server. However, in the case of an entry fop like create
 (/a/b/c) where we want to block modification of layout of b for the
 duration of create, we would be required to acquire lock on the
 subvol to which /a/b hashes. To find out the hashed-subvol of
 /a/b, we would need layout of /a. Note that how there is a dependency
 of locking the layouts of ancestors all the way to root. So this
 locking is not preferred. Also, note that only the immediate parent
 inode is available in arguments of a fop like create.

**Atomicity during namespace operations**

 -  We use locks on inode of parent directory in the namespace of
 _"basename"_  during mkdir, rmdir, renamedir and directory
 creation phase of self-heal. The exact fop we use is _entrylk
 (parent-inode, "basename")_.
 - refresh in-memory layout of parent-inode from values stored on backend
 -  _entrylk (parent-inode, "basename")_ is done on subvolume to which
 _"basename" hashes_. So, this operation is a _reader_ of the
 layout on _parent-inode_. Which means an _inodelk (parent-inode,
 read-lock)_ has to be completed before _entrylk (parent-inode,
 "basename")_ is issued. Both the locks have to be held till the
 operation is tried on all subvolumes. If acquiring of any/all of
 these locks fail, the operation should be failed.

With the above details, algorithms for mkdir, rmdir, renamedir,
self-heal of directory are explicitly detailed below.

**Self-heal of a directory**
  
 -  creation of directories on subvolumes is done only during
 _named-lookup_ of a directory as we need < parent-inode,
 "basename" >.
 -  If a directory is missing on one or more subvolumes,
    *  acquire _inodelk (parent-inode, read-lock)_ on _any one_ of the
 subvolumes.
    * refresh the in-memory layout of parent-inode from values stored on backend
    *  acquire _entrylk (parent-inode, "basename")_ on the subvolume
 to which _"basename"_ hashes.
    *  If any/all of the locks fail, self-heal is aborted.
    *  create directories on missing subvolumes.
    *  release _entrylk (parent-inode, "basename")_.
    *  release _inodelk (parent-inode, read-lock)_.
    
 -  If layout of a directory needs healing
     * acquire _inodelk (directory-inode, write-lock)_ on _all_ the
 subvolumes. If locking fails on any of the subvolumes,
 self-heal is aborted. Blocking Locks are acquired serially across subvolumes in a _well-defined_ order which is _constant_ across all the healers of a directory. One order could be the order in which subvolumes are stored in the array _children_ of dht xlator.
     *  heal the layout.
     *  release _inodelk (directory-inode, write-lock)_ on _all_ the
 subvolumes in parallel.
     *  Note that healing of layout can be done in both _named_ and
 _nameless_ lookups of a directory as _only directory-inode_ is needed
 for healing and it is available during both.

**mkdir (parent-inode, "basename")**

* while creating directory across subvolumes,

 - acquire _inodelk (parent-inode, read-lock)_ on _any one_ of the
 subvolumes.
 - refresh in-memory layout of parent-inode from values stored on backend
 - acquire _entrylk (parent-inode, "basename")_ on the subvolume to
 which _"basename"_ hashes.
 -  If any/all of the above two locks fail, release the locks that
 were acquired successfully and mkdir should be failed (as perceived by application).
 - do _mkdir (parent-inode, "basename")_ on the subvolume to which
 _"basename"_ hashes. If this mkdir fails, mkdir is failed.
 - do _mkdir (parent-inode, "basename")_ on the remaining subvolumes.
 - release _entrylk (parent-inode, "basename")_.
 - release _inodelk (parent-inode, "read-lock")_.
*  while setting the layout of a directory,
 - acquire _inodelk (directory-inode, read-lock)_ on _any one_ of the
 subvolumes.
 -  If locking fails, cleanup the locks that were acquired
 successfully and abort layout setting. Note that we'll have a
 directory without a layout till a lookup happens on the
 directory. This means entry operations within this directory fail
 in this time window. We can also consider failing mkdir. The
 problem of dealing with a directory without layout is out of the
 scope of this document. 
 -  set the layout on _directory-inode_.
 -  release _inodelk (directory-inode, read-lock)_.
* Note that during layout setting we consider mkdir as a  _reader_ not
 _writer_, though it is setting the layout. Reasons are:
 - Before any of other readers like create, link etc that operate on
 this directory to happen, _gfid_ of this directory has to be
 resolved. But _gfid_ is only available only if either of following
 conditions are true: 
      * after mkdir is complete.
      * a lookup on the same path happens parallel to in-progress
 mkdir.

     But, on completion of any of the above two operations, layout
     will be healed. So, none of the _readers_ will happen on a
     directory with partial layout.

* Note that since we've an _entrylk (parent-inode, "basename")_ for
   the entire duration of (attempting) creating directories, parallel
   mkdirs will no longer contend on _mkdir_ on subvolume _to which  "basename"  hashes_. But instead, contend on _entrylk (parent-inode, "basename")_ on the subvolume _to which "basename" hashes_. So, we can attempt the _mkdir_ in _parallel_ on all subvolumes instead of two stage mkdir on hashed first and the rest of them in parallel later. However, we need to make sure that mkdir is successful on the subvolume _to which "basename" hashes_ for mkdir to be successful (as perceived by application). In the case of failed mkdir (as perceived by application), a cleanup should be performed on all the subvolumes before _entrylk (parent-inode, "basename")_ is released.

**rmdir (parent-inode, "basename", directory-inode)**

 -  acquire _inodelk (parent-inode, read-lock)_ on _any one_
 subvolume.
 -  refresh in-memory layout of parent-inode from values stored on backend
 -  acquire _entrylk (parent-inode, "basename")_ on the subvolume to
 which _"basename" hashes_.
 -  If any/all of the above locks fail, rmdir is failed after cleanup
 of the locks that were acquired successfully.
 -  do _rmdir (parent-inode, "basename")_ on the subvolumes to which
 _"basename" doesn't hash to_.
    * If successful, continue. 
    * Else,
     *  recreate directories on those subvolumes where rmdir
  succeeded.
     *  heal the layout of _directory-inode_. Note that this will have
  same synchronization requirements as discussed during layout
  healing part of the section "Directoy self-heal" above.
     *  release _entrylk (parent-inode, "basename")_.
     *  release _inodelk (parent-inode, read-lock)_.
     *  fail _rmdir (parent-inode, "basename")_ to application.
 - do _rmdir (parent-inode, "basename")_ on the subvolume to which
 _"basename" hashes_. 
 - If successful, continue.
 -  Else, Go to the failure part of _rmdir (parent-inode, "basename")_
 on subvolumes to which "basename" _doesn't hash to_. 
 - release _entrylk (parent-inode, "basename")_.
 - release _inodelk (parent-inode, read-lock)_.
 - return success to application.
 
**renamedir (src-parent-inode, "src-basename", src-directory-inode, dst-parent-inode, "dst-basename", dst-directory-inode)**

 -  requirement is to prevent any operation in both _src-namespace_
 and _dst-namespace_. So, we need to acquire locks on both
 namespaces.We also need to have constant ordering while acquiring
 locks during parallel renames of the form _rename (src, dst)_ and
 _rename (dst, src)_ to prevent deadlocks. We can sort gfids of
 _src-parent-inode_ and _dst-parent-inode_ and use that order to
 acquire locks. For the sake of explanation lets say we ended up
 with order of _src_ followed by _dst_.
 -  acquire _inodelk (src-parent-inode, read-lock)_.
 -  refresh in-memory layout of src-parent-inode from values stored on backend
 -  acquire _entrylk (src-parent-inode, "src-basename")_.
 -  acquire _inodelk (dst-parent-inode, read-lock)_.
 -  refresh in-memory layout of dst-parent-inode from values stored on backend
 -  acquire _entrylk (dst-parent-inode, "dst-basename")_.
 - If acquiring any/all of the locks above fail,
      * release the locks that were successfully acquired.
      * fail the renamedir operation to application
      *  done
 - do _renamedir ("src", "dst")_ on the subvolume _to which "dst-basename" hashes_.
    *  If failure, Goto point _If acquiring any/all of the locks above fail_.
    *  else, continue.
 - do _renamedir ("src", "dst")_ on rest of the subvolumes.
    *  If there is any failure, 
      * revert the successful renames.
      *  Goto to point  _If acquiring any/all of the locks above fail_.
    *  else,
        -  release all the locks acquired.
        -  return renamedir as success to application.

**Some examples of races**
This section gives concrete examples of races that can result in inconsistencies explained in the beginning of the document. 

Some assumptions are:

*  We consider an example distribute of three subvols s1, s2 and s3.
*  For examples of renamedir ("src", "dst"), _src_ hashes to s1 and _dst_ hashes to s2. _src_ and _dst_ are associated with _gfid-src_ and _gfid-dst_ respectively
*  For non renamedir examples, _dir_ is the name of directory and it hashes to s1.

And the examples are:  

 -  mkdir vs rmdir - inconsistency in namespace.
   *  mkdir ("dir", gfid1) is complete on s1
   *  rmdir is issued on same directory. Note that, since rmdir needs a gfid, a lookup should be complete before rmdir. lookup creates the directory on rest of the subvols as part of self-heal.
   *  rmdir (gfid1) deletes directory from all subvols.
   *  A new mkdir ("dir", gfid2) is issued. It is successful on s1 associating "dir" with gfid2.
   *  mkdir ("dir", gfid1) resumes and creates directory on s2 and s3 associating "dir" with gfid1.
   *  mkdir ("dir", gfid2) fails with EEXIST on s2 and s3. Since, EEXIST errors are ignored, mkdir is considered successful to application.
   *  In this example we have multiple inconsitencies
      *  "dir" is associated with gfid1 on s2, s3 and with gfid2 on s1
      *  Even if mkdir ("dir", gfid2) was not issued, we would've a case of a directory magically reappearing after a successful rmdir.
 -  lookup heal vs rmdir
   * rmdir ("dir", gfid1) is issued. It is successful on s2 and s3 (non-hashed subvols for name "dir")
   *  lookup ("dir") is issued. Since directory is present on s1 yet, it is created on s2 and s3 associating with gfid1 as part of self-heal
   *  rmdir ("dir", gfid1) is complete on s1 and it is successful
   *  Another lookup ("dir") creates the directory on s1 too
   *  "dir" magically reappears after a successful rmdir
 - lookup heal (src) vs renamedir ("src", "dst")
   *  renamedir ("src", "dst") complete on s2
   *  lookup ("src") recreates _src_ with _gfid-src_ on s2
   *  renamedir ("src", "dst") completes on s1, s3. After rename is complete path _dst_ will be associated with gfid _gfid-src_
   *  Another lookup ("src") recreates _src_ on subvols s1 and s3, associating it with gfid _gfid-src_
   *  Inconsistencies are
      * after a successful renamedir ("src", "dst"), both src and dst exist
      *  Two directories - src and dst - are associated with same gfid. One common symptom is that some entries (of the earlier _src_ and current _dst_ directory) being missed out in readdir listing as the gfid handle might be pointing to the empty healed directory than the actual directory containing entries
 - lookup heal (dst) vs renamedir ("src", "dst")
   *  dst exists and empty when renamdir started
   *  dst doesn't exist when renamedir started
      -  renamedir ("src", "dst") complete on s2 and s3
      -  lookup ("dst") creates _dst_ associating it with _gfid-src_ on s1
      -  An entry is created in _dst_ on either s1
      -  renamedir ("src", "dst") on s1 will result in a directory _dst/dst_ as _dst_ is no longer empty and _man 2 rename_ states that if _dst_ is not empty, _src_ is renamed _as a subdirectory of dst_
      -  A lookup ( _dst/dst_) creates _dst/dst_ on s2 and s3 associating with _gfid-src_ as part of self-heal
      -  Inconsistencies are:
         * Two directories - _dst_ and _dst/dst_ - exist even though both of them didn't exist at the beginning of renamedir
         *  Both _dst_ and _dst/dst_ have same gfid - _gfid-src_. As observed earlier, symptom might be directory listing being incomplete
 - mkdir (dst) vs renamedir ("src", "dst")
 - rmdir (src) vs renamedir ("src", "dst")
 - rmdir (dst) vs renamedir ("src", "dst")