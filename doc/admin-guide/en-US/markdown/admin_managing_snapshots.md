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

*gluster snapshot create \<vol-name\> \[-n \<snap-name\>\] \[-d \<description\>\]*

This command will create a snapshot of a GlusterFS volume. User can provide a snap-name and a description to identify the snap. The description cannot be more than 1024 characters.

Volume should be present and it should be in started state.

**Restoring snaps**

*gluster snapshot restore -v \<vol-name\> \<snap-name\>*

This command restores an already taken snapshot of a GlusterFS volume. Snapshot restore is an offline activity therefore if the volume is online then the restore operation will fail.

Once the snapshot is restored  it will be deleted from the list of snapshot.

**Deleting snaps**

*gluster snapshot delete \<volname\>\ -s \<snap-name\> \[force\]*

This command will delete the specified snapshot.

**Listing of available snaps**

*gluster snapshot list \[\<volname\> \[-s \<snap-name>\]\]*

This command is used to list all snapshots taken, or for a specified volume. If snap-name is provided then it will list the details of that snap.

**Configuring the snapshot behavior**

*gluster snapshot config \[\<vol-name | all\>\]*

This command will display existing config values for a volume. If volume name is not provided then config values of all the volume is displayed. System config is displayed irrespective of volume name.

*gluster snapshot config \<vol-name | all\> \[\<snap-max-hard-limit\> \<count\>\] \[\<snap-max-soft-limit\> \<percentage\>\]*

The above command can be used to change the existing config values. If vol-name is provided then config value of that volume is changed, else it will set/change the system limit.

The system limit is the default value of the config for all the volume. Volume specific limit cannot cross the system limit. If a volume specific limit is not provided then system limit will be considered.

If any of this limit is decreased and the current snap count of the system/volume is more than the limit then the command will fail. If user still want to decrease the limit then force option should be used.



