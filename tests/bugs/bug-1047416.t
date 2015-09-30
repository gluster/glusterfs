#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

function write_to_file {
    dd of=$M0/1 if=/dev/zero bs=1024k count=128 oflag=append 2>&1 >/dev/null
}

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}0 $H0:$B0/${V0}1
TEST $CLI volume start $V0
TEST $CLI volume profile $V0 start
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0

# Verify 'volume profile info' prints both cumulative and incremental stats
write_to_file &
wait
output=$($CLI volume profile $V0 info)
EXPECT 2 cumulative_stat_count "$output"
EXPECT 2 incremental_stat_count "$output" ' 0 '

# Verify 'volume profile info peek' prints both cumulative and incremental stats
# without clearing incremental stats
write_to_file &
wait
output=$($CLI volume profile $V0 info peek)
EXPECT 2 cumulative_stat_count "$output"
EXPECT 2 incremental_stat_count "$output" ' 1 '

write_to_file &
wait
output=$($CLI volume profile $V0 info peek)
EXPECT 2 cumulative_stat_count "$output"
EXPECT 2 incremental_stat_count "$output" ' 1 '

# Verify 'volume profile info incremental peek' prints incremental stats only 
# without clearing incremental stats
write_to_file &
wait
output=$($CLI volume profile $V0 info incremental peek)
EXPECT 0 cumulative_stat_count "$output"
EXPECT 2 incremental_stat_count "$output" ' 1 '

write_to_file &
wait
output=$($CLI volume profile $V0 info incremental peek)
EXPECT 0 cumulative_stat_count "$output"
EXPECT 2 incremental_stat_count "$output" ' 1 '

# Verify 'volume profile info clear' clears both incremental and cumulative stats
write_to_file &
wait
output=$($CLI volume profile $V0 info clear)
EXPECT 2 cleared_stat_count "$output"

output=$($CLI volume profile $V0 info)
EXPECT 2 cumulative_stat_count "$output"
EXPECT 2 incremental_stat_count "$output" ' 0 '
EXPECT 4 data_read_count "$output" ' 0 '
EXPECT 4 data_written_count "$output" ' 0 '

cleanup;
