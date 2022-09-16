#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../dht.rc

TESTS_EXPECTED_IN_LOOP=57
# Initialize
#------------------------------------------------------------
cleanup;

# Start glusterd
TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume info;

# Create a volume
TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2,3};

# Verify volume creation
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';

# Start volume and verify successful start
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';
TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 --entry-timeout=0 $M0;

for i in {1..20}; do
    TEST dd if=/dev/urandom of=${M0}/file.${i} bs=1k count=1 seek=128
done

TEST mkdir $M0/dst

for i in {1..20}; do
    TEST cp --sparse=always ${M0}/file.${i} ${M0}/dst
done 

for i in {1..20}; do
   TEST cmp ${M0}/file.${i} ${M0}/dst/file.${i}
done

cleanup;
