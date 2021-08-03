#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../fileio.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

cold_dir=$(mktemp -d -u -p ${PWD})
TEST mkdir -p ${cold_dir};
#TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2,3,4,5};
TEST $CLI volume create $V0 $H0:$B0/${V0}

TEST $CLI volume set $V0 tier-storetype filesystem;
TEST $CLI volume set $V0 quick-read off;
TEST $CLI volume set $V0 stat-prefetch off;
TEST $CLI volume set $V0 open-behind off;
TEST $CLI volume set $V0 io-cache off;
TEST $CLI volume set $V0 client-io-threads off;
TEST $CLI volume set $V0 read-ahead off;
TEST $CLI volume set $V0 readdir-ahead off;
TEST $CLI volume set $V0 write-behind off;
TEST $CLI volume set $V0 client-log-level DEBUG;
TEST $CLI volume start $V0;

TEST $CLI volume set $V0 tier on;
TEST $CLI volume set $V0 tier-cold-mountpoint ${cold_dir};


#TEST $CLI volume stop $V0;
#TEST $CLI volume start $V0 force;

EXPECT 'Started' volinfo_field $V0 'Status';


sleep 1

## Mount FUSE
TEST $GFS -s $H0 --volfile-id $V0 $M1;

echo hello > $M1/testfile

TEST $CLI volume set $V0 brick-log-level DEBUG;


sleep 2

build_tester $(dirname $0)/active-fd-test.c

TEST cp $(dirname $0)/active-fd-test $M1

# Just try running the command on local file
TEST ! setfattr -n tier.promote-file-as-hot -v true $M1/testfile

/usr/local/share/glusterfs/scripts/migrate-to-cold.sh $M1 $cold_dir $B0/${V0} testfile

ls -lR ${cold_dir}

TEST $M1/active-fd-test $M1/testfile

#TEST rm -rf $M1/*

sleep 1;

ls -lR ${cold_dir}

rm -rvf $cold_dir
TEST rm -f $(dirname $0)/active-fd-test

pkill -USR2 glusterfsd
cleanup;
