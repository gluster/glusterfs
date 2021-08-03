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
TEST $CLI volume set $V0 tier-plugin-migrate-thread disable;
TEST $CLI volume set $V0 quick-read off;
TEST $CLI volume set $V0 stat-prefetch off;
TEST $CLI volume set $V0 write-behind off;
TEST $CLI volume set $V0 open-behind off;

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';
TEST $CLI volume set $V0 brick-log-level DEBUG;

## Mount FUSE
TEST $GFS -s $H0 --volfile-id $V0 $M1;

dd if=/dev/urandom of=/tmp/testfile bs=128k count=128

cp -a /tmp/testfile $M1/testfile

sleep 1;

# Expectation is the migration script gets executed 'relative' to brick path.
bash $pwd_dir/../../../extras/migrate-to-cold.sh $M1 ${cold_dir} $B0/$V0 testfile

getfattr -e text -n tier.migrated-block-count $M1/testfile

#md5sum /tmp/testfile $M1/testfile

dd if=/dev/zero of=$M1/testfile conv=notrunc bs=512 count=5

sleep 2;
setfattr -n tier.promote-file-as-hot -v true $M1/testfile

bash $pwd_dir/../../../extras/migrate-to-cold.sh $M1 ${cold_dir} $B0/$V0 testfile

#echo 3 >/proc/sys/vm/drop_caches

#dd if=$M1/testfile of=/dev/null bs=128k
#md5sum $M1/testfile

dd if=/dev/zero of=$M1/testfile conv=notrunc bs=512 count=6

sleep 2;
setfattr -n tier.promote-file-as-hot -v true $M1/testfile

ls -l $M1/testfile

md5sum $M1/testfile

bash $pwd_dir/../../../extras/migrate-to-cold.sh $M1 ${cold_dir} $B0/$V0 testfile
dd if=/dev/zero of=$M1/testfile conv=notrunc bs=512 count=10
sleep 2;
setfattr -n tier.promote-file-as-hot -v true $M1/testfile
bash $pwd_dir/../../../extras/migrate-to-cold.sh $M1 ${cold_dir} $B0/$V0 testfile
dd if=/dev/zero of=$M1/testfile conv=notrunc bs=512 count=20
sleep 2;
setfattr -n tier.promote-file-as-hot -v true $M1/testfile
bash $pwd_dir/../../../extras/migrate-to-cold.sh $M1 ${cold_dir} $B0/$V0 testfile
sleep 2;
dd if=/dev/zero of=$M1/testfile conv=notrunc bs=512 count=30
setfattr -n tier.promote-file-as-hot -v true $M1/testfile
bash $pwd_dir/../../../extras/migrate-to-cold.sh $M1 ${cold_dir} $B0/$V0 testfile
sleep 2;
dd if=/dev/zero of=$M1/testfile conv=notrunc bs=512 count=40
setfattr -n tier.promote-file-as-hot -v true $M1/testfile


TEST rm -rf ${cold_dir};
cleanup;
