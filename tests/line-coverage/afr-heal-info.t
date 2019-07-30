#!/bin/bash
#Test that parallel heal-info command execution doesn't result in spurious
#entries with locking-scheme granular

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;


function write_and_del_file {
        dd of=$M0/a.txt if=/dev/zero bs=1024k count=100
        rm -f $M0/b.txt
}

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/brick{0,1}
TEST $CLI volume set $V0 locking-scheme granular
TEST $CLI volume start $V0
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;
TEST touch $M0/a.txt $M0/b.txt
write_and_del_file &
touch $B0/f1 $B0/f2

# All above is similar to basic/afr/heal-info.t

TEST $CLI volume heal $V0 enable
TEST $CLI volume heal $V0 info --xml
TEST $CLI volume heal $V0 info summary
TEST $CLI volume heal $V0 info summary --xml
TEST $CLI volume heal $V0 info split-brain
TEST $CLI volume heal $V0 info split-brain --xml

TEST $CLI volume heal $V0 statistics heal-count

# It may fail as the file is not in splitbrain
$CLI volume heal $V0 split-brain latest-mtime /a.txt

TEST $CLI volume heal $V0 disable

TEST $CLI volume stop $V0
cleanup;
