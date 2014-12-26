#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

log_wd=$(gluster --print-logdir)
TEST glusterd
TEST pidof glusterd
rm -f $log_wd/glustershd.log
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume start $V0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0
TEST kill_brick $V0 $H0 $B0/${V0}0
cd $M0
for i in {1..10}
do
        dd if=/dev/urandom of=f bs=1024k count=10 2>/dev/null
done

cd ~
TEST $CLI volume heal $V0 info
function count_inode_link_failures {
        logfile=$1
        grep "inode link failed on the inode" $logfile | wc -l
}
EXPECT "0" count_inode_link_failures $log_wd/glustershd.log
cleanup
