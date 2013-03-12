#!/bin/bash

##Copy this file to tests/bugs before running run.sh (cp extras/test/bug-920583.t tests/bugs/)

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;
logdir=`gluster --print-logdir`

## Start and create a volume
TEST glusterd;
TEST pidof glusterd;

TEST $CLI volume create $V0 replica 2 stripe 2 $H0:$B0/${V0}{1,2,3,4,5,6,7,8};

## Verify volume is is created
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';

## Start volume and verify
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

function log-file-name()
{
    logfilename=$M0".log"
    echo ${logfilename:1} | tr / -
}

log_file=$logdir"/"`log-file-name`

lookup_unhashed_count=`grep "adding option 'lookup-unhashed'" $log_file | wc -l`
no_child_down_count=`grep "adding option 'assert-no-child-down'" $log_file | wc -l`
mount -t glusterfs $H0:/$V0 $M0 -o "xlator-option=*dht.assert-no-child-down=yes,xlator-option=*dht.lookup-unhashed=yes"
touch $M0/file1;

new_lookup_unhashed_count=`grep "adding option 'lookup-unhashed'" $log_file | wc -l`
new_no_child_down_count=`grep "adding option 'assert-no-child-down'" $log_file | wc -l`
EXPECT "1" expr $new_lookup_unhashed_count - $lookup_unhashed_count
EXPECT "1" expr $new_no_child_down_count - $no_child_down_count

## Finish up
TEST $CLI volume stop $V0;
EXPECT 'Stopped' volinfo_field $V0 'Status';

TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0;

cleanup;
