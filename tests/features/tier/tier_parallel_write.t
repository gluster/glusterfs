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

TEST $CLI volume create $V0 $H0:${B0}/${V0}

TEST $CLI volume set $V0 tier-storetype filesystem;
TEST $CLI volume set $V0 quick-read off;
TEST $CLI volume set $V0 write-behind off;
TEST $CLI volume set $V0 stat-prefetch off;
TEST $CLI volume set $V0 storage.reserve 5;

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

TEST $CLI volume set $V0 tier on;
TEST $CLI volume set $V0 tier-cold-mountpoint ${cold_dir};

## Mount FUSE
TEST $GFS -s $H0 --volfile-id $V0 $M1;


cp /etc/hosts $M1/abcd;

sleep 3;

TEST $pwd_dir/../../../extras/migrate-to-cold.sh $M1 ${cold_dir} ${B0}/${V0} ${B0}/${V0}/abcd ;

getfattr -d -m . ${B0}/${V0}/abcd;

mkdir $M1/dir1

fio --exitall_on_error=1  --invalidate=1  --error_dump=1 --direct=1  --verify_dump=1    --ioengine=libaio   --size=100M --rw=write  --name=a1 --bs=128kB --nrfiles=40 --iodepth=128 --directory=$M1/dir1 --group_reporting=1

sleep 3;
sync;

find ${B0}/$V0/ -path "${B0}/${V0}/.glusterfs" -prune -o -type f -exec $pwd_dir/../../../extras/migrate-to-cold.sh $M1 ${cold_dir} ${B0}/${V0}/ {} \;

sync;

TEST $CLI volume set $V0 brick-log-level DEBUG;
sleep 3;

fio --exitall_on_error=1  --invalidate=1  --error_dump=1 --direct=1  --verify_dump=1    --ioengine=libaio   --size=100M --rw=write  --name=a1 --bs=256kb --nrfiles=40 --iodepth=128 --directory=$M1/dir1 --group_reporting=1

sleep 4;

mkdir $M1/dir2
#fio --exitall_on_error=1  --invalidate=1  --error_dump=1 --direct=1  --verify_dump=1    --ioengine=libaio   --size=100M --rw=write  --name=a1 --bs=256KB --nrfiles=40 --iodepth=128 --directory=$M1/dir2 --group_reporting=1


find ${B0}/$V0/ -path "${B0}/${V0}/.glusterfs" -prune -o -type f -exec $pwd_dir/../../../extras/migrate-to-cold.sh $M1 ${cold_dir} ${B0}/${V0}/ {} \;

sleep 3;
fio --exitall_on_error=1  --invalidate=1  --error_dump=1 --direct=1  --verify_dump=1    --ioengine=libaio   --size=100M --rw=write  --name=a1 --bs=256KB --nrfiles=40 --iodepth=128 --directory=$M1/dir1 --group_reporting=1

getfattr -e hex -n tier.migrated-block-count $M1/dir1/a1.0.0
getfattr -e text -n tier.migrated-block-count $M1/dir1/a1.0.2
getfattr -e hex -n tier.migrated-block-count $M1/dir1/a1.0.4

sleep 3;

rm -rf $M1/dir1;

cleanup;

#rm -rf ${cold_dir};
