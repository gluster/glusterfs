#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

function tmp_file_count()
{
    echo $(ls -lh /tmp/tmp.* 2>/dev/null |  wc -l)
}


old_count=$(tmp_file_count);
TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}0 $H0:$B0/${V0}1
TEST $CLI volume start $V0
new_count=$(tmp_file_count);

TEST [ "$old_count" -eq "$new_count" ]

cleanup
