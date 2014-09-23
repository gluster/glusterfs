Managing GlusterFS Volume Snapshots
==========================

This section describes how to perform common GlusterFS volume snapshot
management operations

Pre-requisites
=====================

GlusterFS volume snapshot feature is based on thinly provisioned LVM snapshot.
To make use of snapshot feature GlusterFS volume should fulfill following
pre-requisites:

* Each brick should be on an independent thinly provisioned LVM.
* Brick LVM should not contain any other data other than brick.
* None of the brick should be on a thick LVM.
* gluster version should be 3.6 and above.

Details of how to create thin volume can be found at the following link.
https://access.redhat.com/documentation/en-US/Red_Hat_Enterprise_Linux/6/html/Logical_Volume_Manager_Administration/thinly_provisioned_volume_creation.html


Few features of snapshot are:
=============================

**Crash Consistency**

when a snapshot is taken at a particular point-in-time, it is made sure that
the taken snapshot is crash consistent. when the taken snapshot is restored,
then the data is identical as it was at the time of taking a snapshot.


**Online Snapshot**

When the snapshot is being taken the file system and its associated data
continue to be available for the clients.


**Quorum Based**

The quorum feature ensures that the volume is in good condition while the bricks
are down. Quorum is not met if any bricks are down in a n-way replication where
n <= 2. Quorum is met when m bricks are up, where m >= (n/2 + 1) where n is odd,
and m >= n/2 and first brick is up where n is even. snapshot creation fails
if quorum is not met.


**Barrier**

During snapshot creation some of the fops are blocked to guarantee crash
consistency. There is a default time-out of 2 minutes, if snapshot creation
is not complete within that span then fops are unbarried. If unbarrier happens
before the snapshot creation is complete then the snapshot creation operation
fails. This to ensure that the snapshot is in a consistent state.



Snapshot Management
=====================


**Snapshot creation**

Syntax :
*gluster snapshot create <snapname\> <volname\(s\)\> \[description <description\>\] \[force\]*

Details :
Creates a snapshot of a GlusterFS volume. User can provide a snap-name and a
description to identify the snap. The description cannot be more than 1024
characters.

NOTE : To be able to take a snapshot, volume should be present and it
should be in started state.

-----------------------------------------------------------------------------

**Restoring snaps**

Syntax :
*gluster snapshot restore <snapname\>*

Details :
Restores an already taken snapshot of a GlusterFS volume.
Snapshot restore is an offline activity therefore if the volume is
online (in started state) then the restore operation will fail.

Once the snapshot is restored it will not be available in the
list of snapshots.

---------------------------------------------------------------------------

**Deleting snaps**

Syntax :
*gluster snapshot delete \(all | <snapname\> | volume <volname\>\)*

Details :
If snapname is specified then mentioned snapshot is deleted.
If volname is specified then all snapshots belonging to that particular
volume is deleted. If keyword *all* is used then all snapshots belonging
to the system is deleted.

--------------------------------------------------------------------------

**Listing of available snaps**

Syntax:
*gluster snapshot list \[volname\]*

Details:
Lists  all  snapshots  taken.
If volname is provided, then only the snapshots belonging to
that particular volume is listed.

-------------------------------------------------------------------------

**Information of available snaps**

Syntax:
*gluster snapshot info \[\(snapname | volume <volname\>\)\]*

Details:
This command gives information such as snapshot name, snapshot UUID,
time at which snapshot was created, and it lists down the snap-volume-name,
number of snapshots already taken and number of snapshots still available
for that particular volume, and the state of the snapshot.

------------------------------------------------------------------------

**Status of snapshots**

Syntax:
*gluster snapshot status \[\(snapname | volume <volname\>\)\]*

Details:
This  command  gives  status of the snapshot.
The details included are snapshot brick path, volume group(LVM details),
status of the snapshot bricks, PID of the bricks, data percentage  filled for
that particular volume group to which the snapshots belong to, and total size
of the logical volume.

If snapname is specified then status of the mentioned snapshot is displayed.
If volname  is specified then status of all snapshots belonging to that volume
is displayed. If both snapname and volname is not specified then status of all
the snapshots present in the system are displayed.

------------------------------------------------------------------------

**Configuring the snapshot behavior**

Syntax:
*snapshot config \[volname\] \(\[snap-max-hard-limit <count\>\] \[snap-max-soft-limit <percent>\]\)
                            | \(\[auto-delete <enable|disable\>\]\)*

Details:
Displays and sets the snapshot config values.

snapshot  config without any keywords displays the snapshot config values of
all volumes in the system. If volname is provided, then the snapshot config
values of that volume is  displayed.

Snapshot  config command along with keywords can be used to change the existing
config values. If volname is provided then config value of that volume is
changed, else it  will set/change the system limit.

snap-max-soft-limit  and auto-delete are global options, that will be
inherited by all volumes in the system and cannot be set to individual volumes.

The system limit takes precedence over the volume specific limit.

When auto-delete feature is enabled, then upon reaching the soft-limit,
with every successful snapshot creation, the oldest snapshot will be deleted.

When  auto-delete  feature  is disabled, then upon reaching the soft-limit,
the user gets a warning with every successful snapshot creation.

Upon reaching the hard-limit, further snapshot creations will not be allowed.

-------------------------------------------------------------------------

**Activating a snapshot**

Syntax:
*gluster snapshot activate <snapname\>*

Details:
Activates the mentioned snapshot.

Note : By default the snapshot is activated during snapshot creation.

-------------------------------------------------------------------------

**Deactivate a snapshot**

Syntax:
*gluster snapshot deactivate <snapname\>*

Details:
Deactivates the mentioned snapshot.

-------------------------------------------------------------------------

**Accessing the snapshot**

Snapshots can be activated in 2 ways.

1) Mounting the snapshot:

The snapshot can be accessed via FUSE mount (only fuse). To do that it has to be
mounted first. A snapshot can be mounted via fuse by below command

*mount -t glusterfs <hostname>:/snaps/<snap-name>/<volume-name> <mount-path>*

i.e. say "host1" is one of the peers. Let "vol" be the volume name and "my-snap"
be the snapshot name. In this case a snapshot can be mounted via this command

*mount -t glusterfs host1:/snaps/my-snap/vol /mnt/snapshot*


2) User serviceability:

Apart from the above method of mounting the snapshot, a list of available
snapshots and the contents of each snapshot can be viewed from any of the mount
points accessing the glusterfs volume (either FUSE or NFS or SMB). For having
user serviceable snapshots, it has to be enabled for a volume first. User
serviceability can be enabled for a volume using the below command.

*gluster volume set <volname> features.uss enable*

Once enabled, from any of the directory (including root of the filesystem) an
access point will be created to the snapshot world. The access point is a hidden
directory cding into which will make the user enter the snapshot world. By
default the hidden directory is ".snaps". Once user serviceability is enabled,
one will be able to cd into .snaps from any directory. Doing "ls" on that
directory shows a list of directories which are nothing but the snapshots
present for that volume. Say if there are 3 snapshots ("snap1", "snap2",
"snap3"), then doing ls in .snaps directory will show those 3 names as the
directory entries. They represent the state of the directory from which .snaps
was entered, at different points in time.

NOTE: The access to the snapshots are read-only.

Also, the name of the hidden directory (or the access point to the snapshot
world) can be changed using the below command.

*gluster volume set <volname> snapshot-directory <new-name>*

--------------------------------------------------------------------------------------
