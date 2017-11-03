#Consistent time attributes in gluster across replica/distribute


####Problem:
Traditionally gluster has been using time attributes (ctime, atime, mtime) of files/dirs from bricks. The problem with this approach is that, it is not consisteant  across replica and distribute bricks. And applications which depend on it breaks as replica  might not  always return time attributes from same brick.

Tar especially gives "file changed as we read it" whenever it detects ctime differences when stat is served from  different bricks. The way we have been trying to solve it is to serve  the stat structures from same brick in afr, max-time in dht. But it doesn't avoid the problem completely. Because there is no way to change ctime at the moment(lutimes() only allows mtime, atime), there is little we can do to make sure ctimes match after self-heals/xattr updates/rebalance.

####Solution Proposed:
Store time attribues (ctime, mtime, atime) as an xattr of the file. The xattr is updated based
on the fop. If a filesystem fop changes only mtime and ctime, update only those in xattr for
that file.

####Design Overview:
1) As part of each fop, top layer will generate a time stamp and pass it to the down along
 with other information
> - This will bring a dependency for NTP synced clients along with servers
> - There can be a diff in time if the fop stuck in the xlator for various reason,
for ex: because of locks.

 2)  On the server, posix layer stores the value in the memory (inode ctx) and will sync the data periodically to the disk as an extended attr
> -  Of course sync call also will force it. And fop comes for an inode which is not linked, we do the sync immediately.

 3)  Each time when inodes are created or initialized it read the data from disk and store in inode ctx.

 4)  Before setting to inode_ctx we compare the timestamp stored and the timestamp received, and only store if the stored value is lesser than the current value.

 5)  So in best case data will be stored and retrieved from the memory. We replace the values in iatt with the values in inode_ctx.

 6)  File ops that changes the parent directory attr time need to be consistent across all the distributed directories across the subvolumes. (for eg: a create call will change ctime and mtime of parent dir)

> - This has to handle separately because we only send the fop to the hashed subvolume.
> - We can asynchronously send the timeupdate setattr fop to the other subvoumes and change the values for parent directory if the file fops is successful on hashed subvolume.
> -  This will have a window where the times are inconsistent across dht subvolume (Please provide your suggestions)

7)  Currently we have couple of mount options for time attributes like noatime, relatime , nodiratime etc. But we are not explicitly handled those options even if it is given as mount option when gluster mount.


####Implementation Overview:
This features involves changes in following xlators.
> - utime xlator
> - posix xlator

#####utime xlator:
This is a new client side xlator which does following tasks.

1. It will generate a time stamp and passes it down in frame->root->ctime  and over the network.
2.  Based on fop, it also decides the time attributes to be updated and this passed using "frame->root->flags"

    Patches:
    1. https://review.gluster.org/#/c/19857/

#####posix xlator:
Following tasks are done in posix xlator:

1. Provides APIs to set and get the xattr from backend. It also caches the xattr in inode context. During get, it updates time attributes stored in xattr into iatt structure.
2. Based on the flags from utime xlator, relevant fops update the time attributes in the xattr.

    Patches:
    1. https://review.gluster.org/#/c/19267/
    2. https://review.gluster.org/#/c/19795/
    3. https://review.gluster.org/#/c/19796/

####Pending Work:
1. Handling of time related mount options (noatime, realatime,etc)
2. flag based create (depending on flags in open, create behaviour might change)
3. Changes in dht for direcotory sync acrosss multiple subvolumes
4. readdirp stat need to be worked on.
