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
TEST mkdir -p ${cold_dir};

#TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2,3,4,5};

TEST $CLI volume create $V0 $H0:/opt/brick/${V0}

TEST $CLI volume set $V0 tier-storetype filesystem;
TEST $CLI volume set $V0 quick-read off;
TEST $CLI volume set $V0 write-behind off;
TEST $CLI volume set $V0 stat-prefetch off;
TEST $CLI volume set $V0 storage.reserve 5;

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

TEST $CLI volume set $V0 tier on;
TEST $CLI volume set $V0 tier-cold-mountpoint ${cold_dir};
TEST $CLI volume set $V0 brick-log-level DEBUG;

## Mount FUSE
TEST $GFS -s $H0 --volfile-id $V0 $M1;


cp /etc/hosts $M1/abcd;

sleep 3;

/usr/local/share/glusterfs/scripts/migrate-to-cold.sh $M1 ${cold_dir} /opt/brick/${V0} /opt/brick/${V0}/abcd ;

rm -v ${cold_dir}/abcd.*

getfattr -d -m . /opt/brick/${V0}/abcd;

rm -v $M1/abcd;

mkdir $M1/dir1

fio --exitall_on_error=1  --invalidate=1  --error_dump=1 --direct=1  --verify_dump=1    --ioengine=libaio   --size=100M --rw=write  --name=a1 --bs=1MB --nrfiles=40 --iodepth=128 --directory=$M1/dir1 --group_reporting=1

sleep 3;
sync;

find /opt/brick/$V0/ -path "/opt/brick/${V0}/.glusterfs" -prune -o -type f -exec /usr/local/share/glusterfs/scripts/migrate-to-cold.sh $M1 ${cold_dir} /opt/brick/${V0}/ {} \;

sync;
sleep 3;

#fio --exitall_on_error=1  --invalidate=1  --error_dump=1 --direct=1  --verify_dump=1    --ioengine=libaio   --size=100M --rw=write  --name=a1 --bs=1MB --nrfiles=40 --iodepth=128 --directory=$M1/dir1 --group_reporting=1

