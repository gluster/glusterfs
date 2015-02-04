#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;
# Start glusterd [1]
TEST glusterd

# Create and verify the volume information [2-4]
TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2};
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';

# Turn off the performance translators [5-9]
TEST $CLI volume set $V0 performance.open-behind off
TEST $CLI volume set $V0 performance.write-behind off
TEST $CLI volume set $V0 performance.quick-read off
TEST $CLI volume set $V0 performance.io-cache off
TEST $CLI volume set $V0 performance.read-ahead off

# Start and mount the volume [10-11]
TEST $CLI volume start $V0;
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0

build_tester $(dirname $0)/mandatory-lock-forced.c -o $(dirname $0)/mandatory-lock

# Various read/write tests without enabling mandatory-locking [12-18]
$(dirname $0)/mandatory-lock RD_LCK NONE READ
TEST [ $? -eq 0 ]

$(dirname $0)/mandatory-lock RD_LCK NONE WRITE
TEST [ $? -eq 0 ]

$(dirname $0)/mandatory-lock WR_LCK NONE READ
TEST [ $? -eq 0 ]

$(dirname $0)/mandatory-lock WR_LCK NONE WRITE
TEST [ $? -eq 0 ]

# Specifies O_TRUNC during open
$(dirname $0)/mandatory-lock RD_LCK TRUNC READ
TEST [ $? -eq 0 ]

$(dirname $0)/mandatory-lock RD_LCK NONE FTRUNCATE
TEST [ $? -eq 0 ]

$(dirname $0)/mandatory-lock RD_LCK BLOCK WRITE
TEST [ $? -eq 0 ]

# Enable mandatory-locking [19]
TEST $CLI volume set $V0 mandatory-locking forced

# Restart the volume to take the change into effect [20-23]
TEST umount $M0
TEST $CLI volume stop $V0
TEST $CLI volume start $V0
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0

# Repeat the above tests with mandatory-locking [24-30]
$(dirname $0)/mandatory-lock RD_LCK NONE READ
TEST [ $? -eq 0 ]

EXPECT "Resource temporarily unavailable" $(dirname $0)/mandatory-lock RD_LCK NONE WRITE

EXPECT "Resource temporarily unavailable" $(dirname $0)/mandatory-lock WR_LCK NONE READ

EXPECT "Resource temporarily unavailable" $(dirname $0)/mandatory-lock WR_LCK NONE WRITE

EXPECT "Resource temporarily unavailable" $(dirname $0)/mandatory-lock RD_LCK TRUNC READ

EXPECT "Resource temporarily unavailable" $(dirname $0)/mandatory-lock RD_LCK NONE FTRUNCATE

EXPECT "Resource temporarily unavailable" $(dirname $0)/mandatory-lock RD_LCK BLOCK WRITE

rm -rf $(dirname $0)/mandatory-lock

cleanup

#G_TESTDEF_TEST_STATUS_NETBSD7=KNOWN_ISSUE,BUG=1326464
