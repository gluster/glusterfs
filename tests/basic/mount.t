#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../nfs.rc

cleanup;


## Start and create a volume
TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 replica 2 stripe 2 $H0:$B0/${V0}{1,2,3,4,5,6,7,8};

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


## Make volume tightly consistent for metdata
TEST $CLI volume set $V0 performance.stat-prefetch off;

## Mount FUSE with caching disabled (read-write)
TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id $V0 $M0;

## Check consistent "rw" option
TEST 'mount -t fuse.glusterfs | grep -E "^$H0:$V0 .+ \(rw,"';
TEST 'grep -E "^$H0:$V0 .+ ,?rw," /proc/mounts';

## Mount FUSE with caching disabled (read-only)
TEST glusterfs --entry-timeout=0 --attribute-timeout=0 --read-only -s $H0 --volfile-id $V0 $M1;

## Check consistent "ro" option
TEST 'mount -t fuse.glusterfs | grep -E "^$H0:$V0 .+ \(ro,"';
TEST 'grep -E "^$H0:$V0 .+ ,?ro,.+" /proc/mounts';

## Wait for volume to register with rpc.mountd
EXPECT_WITHIN 20 "1" is_nfs_export_available;

## HACK: mount seems to require about 40 seconds more to be successful,
## sleep 40 is an interim fix till we root cause the underlying issue.
sleep 40

## Mount NFS
TEST mount -t nfs -o nolock,soft,intr $H0:/$V0 $N0;


## Test for consistent views between NFS and FUSE mounts
## write access to $M1 should fail
TEST ! stat $M0/newfile;
TEST ! touch $M1/newfile;
TEST touch $M0/newfile;
TEST stat $M1/newfile;
TEST stat $N0/newfile;
TEST ! rm $M1/newfile;
TEST rm $N0/newfile;
TEST ! stat $M0/newfile;
TEST ! stat $M1/newfile;


## Finish up
TEST $CLI volume stop $V0;
EXPECT 'Stopped' volinfo_field $V0 'Status';

TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0;

cleanup;
