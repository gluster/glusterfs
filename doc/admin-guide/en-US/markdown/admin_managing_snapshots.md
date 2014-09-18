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


Snapshot Management
=====================


**Snapshot creation**

Syntax :
*gluster snapshot create <snapname\> <volname\(s\)\> \[description <description\>\] \[force\]*

Details :
This command will create a snapshot of a GlusterFS volume.
User can provide a snap-name and a description to identify the snap.
The description cannot be more than 1024 characters.

To be able to take a snapshot, Volume should be present and it should be
in started state.

-----------------------------------------------------------------------------

**Restoring snaps**

Syntax :
*gluster snapshot restore <snapname\>*

Details :
This command restores an already taken snapshot of a GlusterFS volume.
Snapshot restore is an offline activity therefore if the volume is
online (in started state) then the restore operation will fail.

Once the snapshot is restored  it will be deleted from the list of snapshot.

---------------------------------------------------------------------------

**Deleting snaps**

Syntax :
*gluster snapshot delete <snapname\>*

Details :
This command will delete the specified snapshot.

--------------------------------------------------------------------------

**Listing of available snaps**

Syntax:
*gluster snapshot list \[volname\]*

Details:
This command is used to list all snapshots taken, or for a specified volume.
If Volname is given, then the snaps belonging to that particular volume
is displayed.

-------------------------------------------------------------------------

**Information of available snaps**

Syntax:
*gluster snapshot info \[\(snapname | volume <volname\>\)\]*

Details:
This command gives out the information such as snapshot name, snapshot UUID,
time at which snapshot was created, and it lists down the snap-volume-name,
Number of snaps taken for a particular volume, number of snaps available
for that particular volume, and the state of snapshot.

------------------------------------------------------------------------

**Status of snapshots**

Syntax:
*gluster snapshot status \[\(snapname | volume <volname\>\)\]*

Details:
This command will give a detailed information about the snapshot.
The details included in this will be Brick-patch of the snapshot bricks,
Volume Group(LVM details) to which the taken snapshot belong to, Status
of the snapshot bricks (whether its running or not), PID of the bricks,
Data percentage filled for that partiuclar Volume Group to which the
snapshots belong to, And total size of the Logical volume to which
the snapshots belong to.

If snapname is specified then details of the mentioned snapshot is displayed.
If Volume name is specified then details of all the snaps belonging to
the mentioned volume name is displayed.
If both snapname and volume name is not specified then details of all
the snaps present will be displayed.

------------------------------------------------------------------------
**Configuring the snapshot behavior**

Syntax:
*gluster snapshot config \[volname\]*

Details:
This command will display existing config values for a volume. If volume name
is not provided then config values of all the volume is displayed.
System config is displayed irrespective of volume name.


Syntax:
*snapshot config \[volname\] \(\[snap-max-hard-limit <count\>\] \[snap-max-soft-limit <percent>\]\)
                            | \(\[auto-delete <enable|disable\>\]\)*

Details:
The above command can be used to change the existing config values.
If volname is provided then config value of that volume is changed,
else it will set/change the system limit.

As of now, snap-max-soft-limit and auto-delete cannot be configured to
individually volume. soft-limit and auto-delete is only applicable
globally. Once that is set, all the volumes will inherit the global
value.

The system limit is the default value of the config for all the volume.
Volume specific limit cannot cross the system limit.
If a volume specific limit is not provided then system limit will be
considered.

When auto-delete feature is enabled, then as soon as the soft-limit
is reached the oldest snapshot is deleted for every successful snapshot
creation, With this it is ensured that number of snapshot created is
not more than snap-max-hard-limit.

When auto-delete feature is disabled, If the the soft-limit is
reached then user is given a warning about exceeding soft-limit
along with successful snapshot creation message (oldest snapshot is
not deleted). And upon reaching hard-limit further snapshot creation
is not allowed.

-------------------------------------------------------------------------

**Activating a snapshot**

Syntax:
*gluster snapshot activate <snapname\>*

Details:
This command will activate the mentioned snapshot.

Note : By default the snapshot is activated during snapshot creation.

-------------------------------------------------------------------------

**Deactivate a snapshot**

Syntax:
*gluster snapshot deactivate <snapname\>*

Details:
This command will de-activate the mentioned snapshot.

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
