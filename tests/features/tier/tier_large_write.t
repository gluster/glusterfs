#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

pwd_dir=$(dirname $0)
cold_dir=$(mktemp -d -u -p ${PWD})
tmp_dir=$(mktemp -d -u -p ${PWD})
TEST mkdir -p ${cold_dir} ${tmp_dir};

#TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2,3,4,5};

TEST $CLI volume create $V0 $H0:$B0/${V0}

TEST $CLI volume set $V0 tier on;
TEST $CLI volume set $V0 tier-storetype filesystem;
TEST $CLI volume set $V0 tier-cold-mountpoint ${cold_dir};
#TEST $CLI volume set $V0 brick-log-level DEBUG;
TEST $CLI volume set $V0 quick-read off;
TEST $CLI volume set $V0 write-behind off;
TEST $CLI volume set $V0 stat-prefetch off;

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

## Mount FUSE
TEST $GFS -s $H0 --volfile-id $V0 $M1;


# Create source files
dd if=/dev/urandom of=${tmp_dir}/testfile bs=128k count=200;
dd if=/dev/urandom of=${tmp_dir}/testfile1 bs=128k count=200;

TEST cp ${tmp_dir}/* $M1/;

sleep 7;

# Expectation is the migration script gets executed 'relative' to brick path.
bash $pwd_dir/../../../extras/migrate-to-cold.sh $M1 ${cold_dir} $B0/$V0 testfile
bash $pwd_dir/../../../extras/migrate-to-cold.sh $M1 ${cold_dir} $B0/$V0 testfile1

#umount $M1;
#TEST $CLI volume set $V0 brick-log-level DEBUG

#TEST $GFS -s $H0 --volfile-id $V0 $M1;
sleep 10;

dd conv=notrunc if=/dev/urandom of=${M1}/testfile bs=4k count=1 seek=0;
dd conv=notrunc if=/dev/urandom of=${M1}/testfile1 bs=64k count=1 seek=20;

ls -l $B0/$V0*/.glusterfs/tier
stat $B0/$V0*/testfile*

sleep 5;

ls -l $B0/$V0*/.glusterfs/tier
stat $B0/$V0*/testfile*

dst_md5sum1=$(md5sum $M1/testfile | awk '{ print $1 }');
dst_md5sum2=$(md5sum $M1/testfile1 | awk '{ print $1 }');

ls -l $M1/;

TEST rm -rf ${cold_dir};
TEST rm -rf ${tmp_dir};
cleanup;
