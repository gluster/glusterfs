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
TEST glusterfs -s $H0 --volfile-id $V0 $M0

TEST touch $M0/file

#setfattr for mandatory-enforcement will fail
TEST ! setfattr -n trusted.glusterfs.enforce-mandatory-lock -v 1 $M0/file

#enable mandatory locking
TEST $CLI volume set $V0 locks.mandatory-locking forced

#setfattr will fail
TEST ! setfattr -n trusted.glusterfs.enforce-mandatory-lock -v 1 $M0/file

#set lock-enforcement option
TEST $CLI volume set $V0 enforce-mandatory-lock on

#setfattr should succeed
TEST setfattr -n trusted.glusterfs.enforce-mandatory-lock -v 1 $M0/file

cleanup;