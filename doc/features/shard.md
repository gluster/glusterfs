### Sharding xlator (Stripe 2.0)

GlusterFS's answer to very large files (those which can grow beyond a
single brick) has never been clear. There is a stripe xlator which allows you to
do that, but that comes at a cost of flexibility - you can add servers only in
multiple of stripe-count x replica-count, mixing striped and unstriped files is
not possible in an "elegant" way. This also happens to be a big limiting factor
for the big data/Hadoop use case where super large files are the norm (and where
you want to split a file even if it could fit within a single server.)

The proposed solution for this is to replace the current stripe xlator with a
new Shard xlator. Unlike the stripe xlator, Shard is not a cluster xlator. It is
placed on top of DHT. Initially all files will be created as normal files, even
up to a certain configurable size. The first block (default 4MB) will be stored
like a normal file. However further blocks will be stored in a file, named by
the GFID and block index in a separate namespace (like /.shard/GFID1.1,
/.shard/GFID1.2 ... /.shard/GFID1.N). File IO happening to a particular offset
will write to the appropriate "piece file", creating it if necessary. The
aggregated file size and block count will be stored in the xattr of the original
(first block) file.

The advantage of such a model:

- Data blocks are distributed by DHT in a "normal way".
- Adding servers can happen in any number (even one at a time) and DHT's
  rebalance will spread out the "piece files" evenly.
- Self-healing of a large file is now more distributed into smaller files across
  more servers.
- piece file naming scheme is immune to renames and hardlinks.

Source: https://gist.github.com/avati/af04f1030dcf52e16535#sharding-xlator-stripe-20

## Usage:

Shard translator is disabled by default. To enable it on a given volume, execute
<code>
gluster volume set <VOLNAME> features.shard on
</code>

The default shard block size is 4MB. To modify it, execute
<code>
gluster volume set <VOLNAME> features.shard-block-size <value>
</code>

When a file is created in a volume with sharding disabled, its block size is
persisted in its xattr on the first block. This property of the file will remain
even if the shard-block-size for the volume is reconfigured later.

If you want to disable sharding on a volume, it is advisable to create a new
volume without sharding and copy out contents of this volume into the new
volume.

## Note:
* Shard translator is still a beta feature in 3.7.0 and will be possibly fully
  supported in one of the 3.7.x releases.
* It is advisable to use shard translator in volumes with replication enabled
  for fault tolerance.

## TO-DO:
* Complete implementation of zerofill, discard and fallocate fops.
* Introduce caching and its invalidation within shard translator to store size
  and block count of shard'ed files.
* Make shard translator work for non-Hadoop and non-VM use cases where there are
  multiple clients operating on the same file.
* Serialize appending writes.
* Manage recovery of size and block count better in the face of faults during
  ongoing inode write fops.
* Anything else that could crop up later :)
