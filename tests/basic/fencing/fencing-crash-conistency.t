#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

# with lock enforcement flag write should fail with out lock

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 $H0:$B0/${V0}1
EXPECT 'Created' volinfo_field $V0 'Status';
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';
TEST $CLI volume set $V0 performance.write-behind off
TEST glusterfs -s $H0 --volfile-id $V0 $M0

TEST touch $M0/file

#write should pass
TEST "echo "test" > $M0/file"
TEST "truncate -s 0 $M0/file"

#enable mandatory locking
TEST $CLI volume set $V0 locks.mandatory-locking forced
TEST $CLI volume set $V0 enforce-mandatory-lock on

#write should pass
TEST "echo "test" >> $M0/file"
TEST "truncate -s 0 $M0/file"

#enforce lock on the file
TEST setfattr -n trusted.glusterfs.enforce-mandatory-lock -v 1 $M0/file

#write should fail
TEST ! "echo "test" >> $M0/file"
TEST ! "truncate -s 0 $M0/file"

#remove lock enforcement flag
TEST setfattr -x trusted.glusterfs.enforce-mandatory-lock $M0/file

#write should pass
TEST "echo "test" >> $M0/file"
TEST "truncate -s 0 $M0/file"

#enforce lock on the file
TEST setfattr -n trusted.glusterfs.enforce-mandatory-lock -v 1 $M0/file
#kill brick
TEST kill_brick $V0 $H0 $B0/${V0}1

TEST $CLI volume start $V0 force

# wait one second for the brick to come online
sleep 2
#write should fail (lock xlator gets lock enforcement info from disk)
TEST ! "echo "test" >> $M0/file"
TEST ! "truncate -s 0 $M0/file"

cleanup;