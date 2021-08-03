#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TESTS_EXPECTED_IN_LOOP=27

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

cold_dir=$(mktemp -d -u -p ${PWD})
TEST mkdir -p ${cold_dir};
#TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2,3,4,5};
TEST $CLI volume create $V0 $H0:$B0/${V0};

TEST $CLI volume set $V0 tier on;
TEST $CLI volume set $V0 tier-storetype filesystem;
TEST $CLI volume set $V0 tier-cold-mountpoint ${cold_dir};
TEST $CLI volume set $V0 quick-read off;
TEST $CLI volume set $V0 stat-prefetch off;

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

## Mount FUSE
TEST $GFS -s $H0 --volfile-id $V0 $M1;

declare -A  before
declare -A  mtime

for i in $(seq 1 10); do
    TEST dd if=/dev/urandom of=$M1/testfile$i bs=128k count=20;
done

umount $M1;

TEST $GFS -s $H0 --volfile-id $V0 $M1;

for i in $(seq 1 10); do
    before[$i]=$(md5sum $M1/testfile$i | awk '{ print $1 }');
    mtime[$i]=`stat -c "%.Y" $M1/testfile$i`
done


sleep 3;

TEST cp -a $M1/testfile* ${cold_dir}/

#post processing of upload
for i in $(seq 1 10); do
    mt=${mtime[$i]}
    TEST setfattr -n tier.mark-file-as-remote -v $mt $M1/testfile$i;
done

umount $M1;

TEST $GFS -s $H0 --volfile-id $V0 $M1;

for i in $(seq 1 10); do
    after=$(md5sum $M1/testfile$i | awk '{ print $1 }');
    b=${before[$i]}
    TEST [ "$after" == "$b" ]
done

getfattr -e text -n tier.remote-read-count $M1/testfile1

TEST rm -rf $cold_dir;

pkill -USR2 gluster;
cleanup;
