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

TEST $CLI volume set $V0 quick-read off;
TEST $CLI volume set $V0 storage.reserve 5;

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

TEST $CLI volume set $V0 brick-log-level DEBUG;
TEST $CLI volume set $V0 write-behind off;
TEST $CLI volume set $V0 stat-prefetch off;

TEST $CLI volume set $V0 tier-stub-size 256KB;


gluster volume set ${V0} features.tier-stub-size 256KB
gluster volume set ${V0} nfs.disable on
gluster volume set ${V0} storage.fips-mode-rchecksum on
gluster volume set ${V0} transport.address-family inet
gluster volume set ${V0} features.tier enable
gluster volume set ${V0} diagnostics.brick-sys-log-level WARNING
gluster volume set ${V0} diagnostics.client-sys-log-level WARNING
gluster volume set ${V0} cluster.readdir-optimize on
gluster volume set ${V0} ctime.noatime off
gluster volume set ${V0} diagnostics.count-fop-hits off
gluster volume set ${V0} diagnostics.latency-measurement off
gluster volume set ${V0} disperse.eager-lock off
gluster volume set ${V0} disperse.other-eager-lock off
gluster volume set ${V0} features.cache-invalidation on
gluster volume set ${V0} features.cache-invalidation-timeout 600
gluster volume set ${V0} features.ctime off
gluster volume set ${V0} features.selinux off
gluster volume set $V0 tier-cold-mountpoint ${cold_dir}
gluster volume set ${V0} network.inode-lru-limit 90000
gluster volume set ${V0} performance.cache-invalidation on
gluster volume set ${V0} performance.io-cache off
gluster volume set ${V0} performance.md-cache-statfs off
gluster volume set ${V0} performance.md-cache-timeout 300
gluster volume set ${V0} performance.stat-prefetch off
gluster volume set ${V0} performance.io-thread-count 16
gluster volume set ${V0} config.client-threads 4
gluster volume set ${V0} config.brick-threads 16
gluster volume set ${V0} server.event-threads 8
TEST $CLI volume set ${V0} features.tier-storetype filesystem
gluster volume set ${V0} client.event-threads 8
gluster volume set ${V0} performance.write-behind off
gluster volume set ${V0} performance.open-behind off
gluster volume set ${V0} performance.quick-read off
gluster volume set ${V0} storage.health-check-interval 0
gluster volume set ${V0} performance.client-io-threads on
gluster volume set ${V0} performance.read-ahead on
gluster volume set ${V0} storage.reserve-size 25

## Mount FUSE
TEST $GFS -s $H0 --volfile-id $V0 $M1;


cp /etc/hosts $M1/abcd;

sleep 3;

/usr/local/share/glusterfs/scripts/migrate-to-cold.sh $M1 ${cold_dir} /opt/brick/${V0} /opt/brick/${V0}/abcd ;

getfattr -d -m . /opt/brick/${V0}/abcd;

mkdir $M1/dir1

sync;
sleep 3;

setfattr -n tier.promote-file-as-hot -v true $M1/abcd;

#fio --exitall_on_error=1  --invalidate=1  --error_dump=1 --direct=1  --verify_dump=1    --ioengine=libaio   --size=100M --rw=write  --name=a1 --bs=1MB --nrfiles=40 --iodepth=128 --directory=$M1/dir1 --group_reporting=1

