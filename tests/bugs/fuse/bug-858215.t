#!/bin/bash

. $(dirname $0)/../../include.rc

cleanup;


## Start and create a volume
TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume info;

TEST $CLI volume create $V0 replica 2 stripe 2 $H0:$B0/${V0}{1,2,3,4,5,6,7,8};
TEST $CLI volume set $V0 nfs.disable off

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

## Mount FUSE with caching disabled
TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id $V0 $M0;

## Test for checking whether the fops have been saved in the event-history
TEST ! stat $M0/newfile;
TEST touch $M0/newfile;
TEST stat $M0/newfile;
TEST rm $M0/newfile;

nfs_pid=$(cat $GLUSTERD_WORKDIR/nfs/run/nfs.pid);
glustershd_pid=$(cat $GLUSTERD_WORKDIR/glustershd/run/glustershd.pid);

pids=$(pidof glusterfs);
for i in $pids
do
        if [ $i -ne $nfs_pid ] && [ $i -ne $glustershd_pid ]; then
                mount_pid=$i;
                break;
        fi
done

dump_dir='/tmp/gerrit_glusterfs'
cat >$statedumpdir/glusterdump.options <<EOF
all=yes
path=$dump_dir
EOF

TEST mkdir -p $dump_dir;
TEST kill -USR1 $mount_pid;
sleep 2;
for file_name in $(ls $dump_dir)
do
    TEST grep -q "xlator.mount.fuse.history" $dump_dir/$file_name;
done

## Finish up
TEST $CLI volume stop $V0;
EXPECT 'Stopped' volinfo_field $V0 'Status';

TEST $CLI volume delete $V0;
TEST ! $CLI volume info $V0;

TEST rm -rf $dump_dir;
TEST rm $statedumpdir/glusterdump.options;

cleanup;
