#!/bin/bash

. $(dirname $0)/../include.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

function volinfo_field()
{
    local vol=$1;
    local field=$2;

    $CLI volume info $vol | grep "^$field: " | sed 's/.*: //';
}

TEST $CLI volume create $V0 $H0:$B0/brick1;
EXPECT 'Created' volinfo_field $V0 'Status';

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

## Wait for volume to register with rpc.mountd
sleep 5;

#mount on a random dir
TEST MOUNTDIR="/tmp/$RANDOM"
TEST mkdir $MOUNTDIR
TEST  mount -t nfs -o vers=3,nolock,soft,intr $H0:/$V0 $MOUNTDIR;
flag=0

TEST touch $MOUNTDIR/testfile

TEST GEOREPDIR="/tmp/$RANDOM"
TEST mkdir $GEOREPDIR

TEST $CLI volume geo-replication $V0 file:///$GEOREPDIR start

for i in {1..500}; do cat /etc/passwd >> $MOUNTDIR/testfile; if [ $? -ne 0 ]; then flag=1; break; fi; done
TEST [ $flag -eq 0 ]
TEST rm -rf $GEOREPDIR

TEST   umount $MOUNTDIR
TEST   rm -rf $MOUNTDIR

cleanup;
