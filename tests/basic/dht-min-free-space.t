#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../traps.rc

grep $B0/patchy1 /proc/mounts &> /dev/null && umount $B0/patchy1
grep $B0/patchy2 /proc/mounts &> /dev/null && umount $B0/patchy2
mkdir $B0/${V0}{1..2}

TEST glusterd

TEST truncate --size $((30*1048576)) $B0/${V0}-dev1
push_trapfunc "rm -f $B0/${V0}-dev1"
TEST truncate --size $((30*1048576)) $B0/${V0}-dev2
push_trapfunc "rm -f $B0/${V0}-dev2"

TEST mkfs.xfs $B0/${V0}-dev1
TEST mkfs.xfs $B0/${V0}-dev2

TEST mount -o loop $B0/${V0}-dev1 $B0/${V0}1
TEST mount -o loop $B0/${V0}-dev2 $B0/${V0}2

TEST $CLI volume create $V0 $H0:$B0/${V0}1 $H0:$B0/${V0}2
TEST $CLI volume set $V0 cluster.min-free-disk 2MB
TEST $CLI volume set $V0 cluster.min-free-strict-mode on
TEST $CLI volume set $V0 cluster.du-refresh-interval-sec 0
TEST $CLI volume start $V0

TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0

####################################
# Test re-directs of file creation #
####################################

# This should work, no redirects
TEST dd if=/dev/zero of=$M0/testfile1 bs=1M count=8
TEST [ -f /d/backends/${V0}2/testfile1 ] && [ ! -k /d/backends/${V0}1/testfile1 ]

TEST $CLI volume set $V0 cluster.min-free-disk 19MB

# This should work, & the file redirected
# Subvolume 2 should have the linkto &
# Subvolume 1 should have the original
TEST dd if=/dev/zero of=$M0/testfile3 bs=1M count=4
TEST [ -f /d/backends/${V0}1/testfile3 ] && [ ! -k /d/backends/${V0}1/testfile3 ]
TEST [ -k /d/backends/${V0}2/testfile3 ]

# This should fail, cluster is full
TEST ! dd if=/dev/zero of=$M0/testfile2 bs=1M count=23

###################
# Strict mode off #
###################
TEST $CLI volume set $V0 cluster.min-free-strict-mode off
TEST dd if=/dev/zero of=$M0/testfile1 bs=1M count=20
TEST rm -f $M0/testfile1

###################
# Strict mode on #
###################
TEST $CLI volume set $V0 cluster.min-free-strict-mode on
TEST ! dd if=/dev/zero of=$M0/testfile1 bs=1M count=16
TEST rm -f $M0/testfile1

# Cleanup will deal with our mounts for us, and (because we used "-o loop") our
# device files too, but not the underlying files.  That will happen in the EXIT
# trap handler instead.
cleanup;
