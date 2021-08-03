#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

pwd_dir=$(dirname $0)
cold_dir=$(mktemp -d -u -p ${PWD})
TEST mkdir -p ${cold_dir};

#TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2,3,4,5};

TEST $CLI volume create $V0 $H0:$B0/${V0}

TEST $CLI volume set $V0 tier on;
TEST $CLI volume set $V0 tier-storetype filesystem;
TEST $CLI volume set $V0 tier-cold-mountpoint ${cold_dir};
TEST $CLI volume set $V0 quick-read off;
TEST $CLI volume set $V0 stat-prefetch off;

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

## Mount FUSE
TEST $GFS -s $H0 --volfile-id $V0 $M1;

echo hello > $M1/testfile;

umount $M1;

TEST $GFS -s $H0 --volfile-id $V0 $M1;

TEST cat $M1/testfile;

sleep 1;

# Expectation is the migration script gets executed 'relative' to brick path.
bash $pwd_dir/../../../extras/migrate-to-cold.sh $M1 ${cold_dir} $B0/$V0 testfile

umount $M1;

TEST $GFS -s $H0 --volfile-id $V0 $M1;

echo hello >> $M1/testfile;

TEST cat $M1/testfile;

TEST rm -rf ${cold_dir};
cleanup;
