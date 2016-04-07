#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

function check_brick_inter_stats() {
  local counter="$1"
  local inter_cnt=""

  inter_cnt=$(grep -h "\".*inter.*$counter\"" \
    /var/lib/glusterd/stats/glusterfsd*.dump 2>/dev/null |
    grep -v '\"0.0000\"' | wc -l)
  if (( $inter_cnt == 3 )); then
    echo "Y"
  else
    echo "N"
  fi
}

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2}
TEST $CLI volume set $V0 diagnostics.stats-dump-interval 5
TEST $CLI volume set $V0 diagnostics.count-fop-hits on
TEST $CLI volume set $V0 diagnostics.latency-measurement on
TEST $CLI volume start $V0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0

# Generate some FOPs
cd $M0
for i in {1..10}; do
  mkdir a
  cd a
  for g in {1..10}; do
    dd if=/dev/zero of=test$g bs=128k count=1
  done
done

EXPECT_WITHIN 30 "Y" check_brick_inter_stats fop.weighted_latency_ave_usec

cleanup
