#!/bin/bash
#
# Test for the Halo hybrid feature
#
# 1. Create volume w/ 3x replication w/ max-replicas = 2 for clients,
#    heal daemon is off to start.
# 2. Write some data
# 3. Verify hybrid code chose children for lookups
# 4. Verify hybrid code chose child for reads
# 5. Verify hybrid code wrote synchronously to all replicas
#

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

function found_fuse_log_msg {
  local dir="$1"
  local msg="$2"
  local cnt=$(cat /var/log/glusterfs/$M0LOG | grep "$msg" | tail -n1 | wc -l)
  if (( $cnt == 1 )); then
    echo "Y"
  else
    echo "N"
  fi
}

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2}
TEST $CLI volume set $V0 cluster.background-self-heal-count 0
TEST $CLI volume set $V0 cluster.shd-max-threads 1
TEST $CLI volume set $V0 cluster.halo-enabled True
TEST $CLI volume set $V0 cluster.halo-hybrid-mode True
TEST $CLI volume set $V0 cluster.heal-timeout 5
TEST $CLI volume set $V0 cluster.self-heal-daemon off
TEST $CLI volume set $V0 cluster.eager-lock off
TEST $CLI volume set $V0 cluster.choose-local off
TEST $CLI volume set $V0 diagnostics.client-log-level TRACE
TEST $CLI volume start $V0

# Start a synchronous mount
TEST glusterfs --volfile-id=/$V0 \
  --xlator-option *replicate*.halo-max-latency=9999  \
  --volfile-server=$H0 $M0 \
  --attribute-timeout=0 --entry-timeout=0
sleep 2
cd $M0

TEST mkdir testdir
TEST cd testdir
for i in {1..5}
do
        dd if=/dev/urandom of=testfile$i bs=1M count=1 2>/dev/null
done
TEST ls -l

EXPECT_WITHIN "60" "Y" found_fuse_log_msg "children for LOOKUPs"
EXPECT_WITHIN "60" "Y" found_fuse_log_msg "Selected hybrid child"

B0_CNT=$(ls $B0/${V0}0/testdir | wc -l)
B1_CNT=$(ls $B0/${V0}1/testdir | wc -l)
B2_CNT=$(ls $B0/${V0}2/testdir | wc -l)

# Writes should be synchronous, all should have same
# file count
TEST "(($B0_CNT == 5 && $B1_CNT == 5 && $B2_CNT == 5))"

cleanup
