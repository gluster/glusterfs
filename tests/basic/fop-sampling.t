#!/bin/bash
#

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

SAMPLE_FILE="$(gluster --print-logdir)/samples/glusterfs_${V0}.samp"

function print_cnt() {
  local FOP_TYPE=$1
  local FOP_CNT=$(grep ,${FOP_TYPE} ${SAMPLE_FILE} | wc -l)
  echo $FOP_CNT
}

# Verify we got non-zero counts for stats/lookup/readdir
check_samples() {
        STAT_CNT=$(print_cnt STAT)
        if [ "$STAT_CNT" -le "0" ]; then
                echo "STAT count is zero"
                return
        fi

        LOOKUP_CNT=$(print_cnt LOOKUP)
        if [ "$LOOKUP_CNT" -le "0" ]; then
                echo "LOOKUP count is zero"
                return
        fi

        READDIR_CNT=$(print_cnt READDIR)
        if [ "$READDIR_CNT" -le "0" ]; then
                echo "READDIR count is zero"
                return
        fi

        echo "OK"
}

cleanup;
TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2}
TEST $CLI volume set $V0 nfs.disable off
TEST $CLI volume set $V0 diagnostics.latency-measurement on
TEST $CLI volume set $V0 diagnostics.count-fop-hits on
TEST $CLI volume set $V0 diagnostics.stats-dump-interval 2
TEST $CLI volume set $V0 diagnostics.fop-sample-buf-size 65535
TEST $CLI volume set $V0 diagnostics.fop-sample-interval 1
TEST $CLI volume set $V0 diagnostics.stats-dnscache-ttl-sec 3600

TEST $CLI volume start $V0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0

for i in {1..5}
do
        dd if=/dev/zero of=${M0}/testfile$i bs=4k count=1
done

TEST ls -l $M0
EXPECT_WITHIN 6 "OK" check_samples

cleanup
