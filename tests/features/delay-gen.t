#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/${V0}1

EXPECT "$V0" volinfo_field $V0 'Volume Name'
EXPECT 'Created' volinfo_field $V0 'Status'

TEST $CLI volume set $V0 delay-gen posix
TEST $CLI volume set $V0 delay-gen.delay-duration 1000000
TEST $CLI volume set $V0 delay-gen.delay-percentage 100
TEST $CLI volume set $V0 delay-gen.enable read,write

TEST $CLI volume start $V0
EXPECT 'Started' volinfo_field $V0 'Status'

TEST $CLI volume profile $V0 start
## Mount FUSE with caching disabled (read-write)
TEST $GFS -s $H0 --volfile-id $V0 $M0

TEST dd if=/dev/zero of=$M0/1 count=1 bs=128k oflag=sync

#Write should take at least a second
write_max_latency=$($CLI volume profile $V0 info | grep WRITE | awk 'BEGIN {max = 0} {if ($6 > max) max=$6;} END {print max}' | cut -d. -f 1 | egrep "[0-9]{10,}")

#Create should not take a second
create_max_latency=$($CLI volume profile $V0 info | grep CREATE | awk 'BEGIN {max = 0} {if ($6 > max) max=$6;} END {print max}' | cut -d. -f 1 | egrep "[0-9]{10,}")

TEST [ ! -z $write_max_latency ];
TEST [ -z $create_max_latency ];

# Not providing a particular fop will make it test everything
TEST $CLI volume reset $V0 delay-gen.enable
TEST $CLI volume set $V0 delay-gen.delay-duration 100

cp $(dirname ${0})/../basic/gfapi/glfsxmp-coverage.c glfsxmp.c
build_tester ./glfsxmp.c -lgfapi
./glfsxmp $V0 $H0 >/dev/null
cleanup_tester ./glfsxmp
rm ./glfsxmp.c

$(dirname $0)/../basic/rpc-coverage.sh $M0 >/dev/null

cleanup;
#G_TESTDEF_TEST_STATUS_NETBSD7=1501397
