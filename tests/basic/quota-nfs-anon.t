#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

TEST glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}{1}

function volinfo_field()
{
    local vol=$1;
    local field=$2;

    $CLI volume info $vol | grep "^$field: " | sed 's/.*: //';
}


## Verify volume is is created
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';


## Start volume and verify
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

TEST $CLI volume quota $V0 enable;

## Mount NFS
TEST mount -t nfs -o nolock,soft,intr $H0:/$V0 $N0;
mkdir -p $N0/0/1
TEST $CLI volume quota $V0 limit-usage /0/1 1GB 75%;

deep=/0/1/2/3/4/5/6/7/8/9
mkdir -p $N0/$deep
dd if=/dev/zero of=$N0/$deep/file bs=1M count=502 &

kill_brick $V0 $H0 $B0/${V0}{1}
kill -TERM $(get_nfs_pid)

$CLI volume start $V0 force;


cleanup;
