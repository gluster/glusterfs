#!/bin/bash

. $(dirname $0)/../../include.rc

cleanup;

## Start and create a volume
TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume info;

TEST $CLI volume create $V0 replica 2 stripe 2 $H0:$B0/${V0}{1,2,3,4,5,6,7,8};

function volinfo_field()
{
    local vol=$1;
    local field=$2;

    $CLI volume info $vol | grep "^$field: " | sed 's/.*: //';
}

dump_dir='/tmp/gerrit_glusterfs'
TEST mkdir -p $dump_dir;
## Verify volume is is created
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';
TEST $CLI volume set $V0 error-gen posix;
TEST $CLI volume set $V0 server.statedump-path $dump_dir;

## Start volume and verify
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

TEST PID=`gluster --xml volume status patchy | grep -A 8 patchy1 | grep '<pid>' | cut -d '>' -f 2 | cut -d '<' -f 1`
TEST kill -USR1 $PID;
sleep 2;
for file_name in $(ls $dump_dir)
do
    TEST grep "error-gen.priv" $dump_dir/$file_name;
done

## Finish up
TEST $CLI volume stop $V0;
EXPECT 'Stopped' volinfo_field $V0 'Status';

TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0;

TEST rm -rf $dump_dir;

cleanup;
