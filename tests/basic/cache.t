#!/bin/bash
#

FILE=/var/log/glusterfs/samples/glusterfs_patchy.samp
rm $FILE

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

function print_cnt() {
  local FOP_TYPE=$1
  local FOP_CNT=$(grep ,${FOP_TYPE} $FILE | wc -l)
  echo $FOP_CNT
}

function print_avg() {
  local FOP_TYPE=$1
  local FILE=/var/log/glusterfs/samples/glusterfs_patchy.samp
  local FOP_AVG=$(grep -oE "${FOP_TYPE},[0-9]+\." ${FILE} | grep -oE '[0-9]+' | awk 'NR == 1 { sum = 0 } { sum += $1; } END {printf "%d", sum/NR}')
  echo $FOP_AVG
}

cleanup;
TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2}
TEST $CLI volume set $V0 nfs.disable off
TEST $CLI volume set $V0 diagnostics.latency-measurement on
TEST $CLI volume set $V0 diagnostics.count-fop-hits on
TEST $CLI volume set $V0 diagnostics.fop-sample-buf-size 65535
TEST $CLI volume set $V0 diagnostics.fop-sample-interval 1
TEST $CLI volume set $V0 diagnostics.stats-dump-interval 1
TEST $CLI volume start $V0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0

for i in {1..100}
do
        df $M0 &> /dev/null
done

sleep 6

# Get average
STATFS_CNT0=$(print_cnt STATFS)
TEST [ "$STATFS_CNT0" -gt "0" ]
STATFS_AVG0=$(print_avg STATFS)
# Make it easier to compute averages
rm $FILE

TEST $CLI volume set $V0 performance.nfs.io-cache on
TEST $CLI volume set $V0 performance.statfs-cache on
TEST $CLI volume set $V0 performance.statfs-cache-timeout 10

for i in {1..100}
do
        df $M0 &> /dev/null
done

sleep 6

# Get average
STATFS_CNT1=$(print_cnt STATFS)
TEST [ "$STATFS_CNT1" -eq "$STATFS_CNT0" ]
STATFS_AVG1=$(print_avg STATFS)

# Verify that cached average * 10 is still faster than uncached
STATFS_AVG1x10=$(($STATFS_AVG1 * 10))
TEST [ "$STATFS_AVG0" -gt "$STATFS_AVG1x10" ]
#cleanup
