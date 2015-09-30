#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info

# Create a 1x1 distributed volume
TEST $CLI volume create $V0 $H0:$B0/${V0}0
EXPECT 'Created' volinfo_field $V0 'Status'

# Start volume
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status'

# Mount volume over FUSE
TEST glusterfs -s $H0 --volfile-id $V0 $M0

TEST mkdir $M0/dir
TEST touch $M0/dir/a
TEST ln $M0/dir/a $M0/dir/b

# Confirm hardlinks
inum1=$(ls -i $M0/dir/a | cut -d' ' -f1)
inum2=$(ls -i $M0/dir/b | cut -d' ' -f1)
TEST [ "$inum1" = "$inum2" ]

# Turn on build-pgfid
TEST $CLI volume set $V0 build-pgfid on
EXPECT 'on' volinfo_field $V0 'storage.build-pgfid'

# Unlink files
TEST unlink $M0/dir/a
TEST unlink $M0/dir/b

# Unmount
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

# Stop the volume
TEST $CLI volume stop $V0;
EXPECT 'Stopped' volinfo_field $V0 'Status';

# Delete the volume
TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0;

cleanup
