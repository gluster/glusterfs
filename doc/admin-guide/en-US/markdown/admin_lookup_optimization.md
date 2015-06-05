# DHT lookup optimization

Distribute xlator (or DHT) has a performance penalty when dealing with negative
lookups. This document explains the problem and optimization provided for
alleviating the same in GlusterFS.

## Negative lookups and issues surrounding them

Negative lookups are lookup operations for entries that are not present in the
volume. IOW, a lookup for a file/directory that does not exist is a negative
lookup.

DHT normally looks up an entry in the hashed subvolume first (based on the
layout), if not found in the hashed location, it fans out a lookup across all
subvolumes to DHT, to ensure that the entry is not present in another subvolume.
This behavior comes from the perspective that if a rebalance is in progress,
and the layout on disk is temporarily out of alignment with the actual location
of the file, the entry is still found by the fan out lookup.

Such fan out lookups are costly and typically slow down file creates. This
especially impacts small file performance, where a large number of files are
being added/created in quick succession to the volume.

## Optimizing lookups in DHT

A balanced volume is either, a new volume that is created, and no bricks are
added to, or removed from the same, or a volume that has undergone expansion
(or reduction) of bricks and a full rebalance has been run on the volume.

In such volumes, the fan out lookup behavior can be turned off to speed up
negative lookups, as files are in their respective hashed locations (or at
least their DHT link-to entries are present in the hashed location).

With GlusterFS 3.7.2 negative lookup fan-out behavior is optimized, by not
performing the same, in an balanced volume.

The optimization provided, further detects a cluster out of balance (when a
fix-layout is done, or a brick is removed) to automatically turn **on** the
fan out negative lookup behavior, thereby preventing duplicate entry creation
in the volume, till the volume is brought into balance again.

## Configuration options to enable optimized lookups

With Gluster 3.7.2 the following options are provided to enable DHT lookup
optimization,

Option: cluster.lookup-optimize
Description: "This option if set to ON enables the optimization of -ve lookups,
by not doing a lookup on non-hashed subvolumes for files, in case the hashed
subvolume does not return any result. This option disregards the
lookup-unhashed setting, when enabled."
Default: OFF

CLI command to enable this option:
  gluster volume set <volname> cluster.lookup-optimize <on/off>

### Client compatibility support

As DHT xlator runs on the client stack of gluster (i.e on the machine where the
FUSE/NFS Server/SAMBA Server are running), this configuration requires that the
cluster and the clients are upgraded to 3.7.2 version, at the minimum.

When setting this option, if any Gluster brick node or connected clients are of
an older version, the option will error out stating incompatible version
detected in the cluster and not allow the configuration change.

Older clients connecting to the cluster post this configuration option is set,
would also error out and not be able to mount the volume due to the version
incompatibility.

### Compatability with lookup-unhashed setting

In older DHT versions, the configuration option lookup-unhashed emulated a
similar behavior for a balanced cluster. The downside of this option is that
if the cluster grows or becomes unbalanced, there is a risk of losing entry
consistency. The current changes to gluster and specifically in DHT, prevent
this inconsistency from occurring when using the new option (lookup-optimize).

Additionally, if the lookup-optimize option is set, the older lookup-unhashed
setting is ignored by DHT.

##  Requirements for the optimization to function

When the lookup-optimize option is enabled, there are a few prerequisites
before which the option is honored by DHT. The following list provides some of
these conditions and ways to meet the same.

1. New volume
  A new volume is a volume that has just been created and is unused or not
  started
  - For a volume that is just created
    - Prerequisite: Before starting and accessing this volume, set the lookup
    optimization to ON
    - Gotchas: All directories that are created post the above setting, will
    leverage the negative lookup optimization, except entries in the root of
    the volume.
      NOTE: The root of the volume, or the brick root on each brick of the
      volume, is already created prior to the start of the volume, or the
      ability to set this option. As a result the root of the volume gains this
      optimization only post the first full rebalance, or is treated equivalent
      to an existing directory (see (2)-(1) below).

2. Existing volume
  An existing volume is one which is under use, and may have had bricks added
  or removed in its lifetime. In this scenario there are 2 cases where the
  lookup optimization behavior changes,
  Prerequisite: Enable the lookup-optimize option
  1. New directory creation
    - All directories created beyond this point will gain the negative lookup
    optimization
  2. Existing directories
    - Existing directories will not gain the lookup optimization
    - To enable existing directories to also gain the lookup optimization a
    full rebalance on the volume needs to be performed

The optimization is also bypassed by the code automatically in the following
conditions,

1. Brick removed
  - When a remove-brick is executed for a volume, it immediately triggers a
  rebalance to move data out of the removed bricks. In these circumstances the
  optimization is bypassed and a fan out lookup is performed for negative
  lookups.
  - Post removal of the brick, the lookup optimization would automatically kick
  in
2. Brick added and only fix-layout is executed
  - When a brick is added and a fix-layout only is executed, the files are
  still not present in the correct hashed locations. As a result under these
  conditions the lookup optimization is bypassed by DHT.
  - A full rebalance post fix-layout would get the optimization enabled
    NOTE: Although fix-layout is deprecated, it is still present and honored,
    as a result this distinction is presented in this document. This is not an
    endorsement of fix-layout still being supported.

## FAQ
<< TBD >>
1. How do I verify that I have a problem with negative lookups? OR
  When should I use this option?
2. Can I roll back to an older client post enabling this optimization?
3. How do I verify this option is working for me?
4. What additional meta-data does this option add to the bricks?
5. I see duplicate entries after enabling this option, what should I do?
6. I see the following error in my client logs, help!
7. My create performance is still poor, help!
8. <Other suggestions welcome>
