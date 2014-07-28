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
