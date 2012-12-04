#!/bin/bash
. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

B0_hiphenated=`echo $B0 | tr '/' '-'`
TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/r2d2_0 $H0:$B0/r2d2_1 $H0:$B0/r2d2_2 $H0:$B0/r2d2_3
TEST $CLI volume set $V0 self-heal-daemon off
TEST $CLI volume set $V0 data-self-heal off
TEST $CLI volume set $V0 metadata-self-heal off
TEST $CLI volume set $V0 entry-self-heal off
TEST $CLI volume start $V0
TEST glusterfs --volfile-server=$H0 --volfile-id=/$V0 $M0

#test case of 858212
for i in {13,14,16}; do echo abc > $M0/$i; done
TEST mv -f $M0/14 $M0/13
kill -9 $(cat /var/lib/glusterd/vols/$V0/run/$H0$B0_hiphenated-r2d2_3.pid)
TEST $CLI volume add-brick $V0 $H0:$B0/r2d2_4 $H0:$B0/r2d2_5
TEST $CLI volume rebalance $V0 start
EXPECT_WITHIN 20 "completed" rebalance_status_completed_field
TEST mv -f $M0/13 $M0/16
$CLI volume remove-brick $V0 $H0:$B0/r2d2_{4,5} start
EXPECT_WITHIN 20 "completed" remove_brick_status_completed_field
$CLI volume remove-brick $V0 $H0:$B0/r2d2_{4,5} commit
TEST mv -f $M0/16 $M0/13
TEST $CLI volume set $V0 self-heal-daemon on
TEST $CLI volume start $V0 force
EXPECT_WITHIN 10 "1" afr_child_up_status $V0 3
EXPECT_WITHIN 10 "Y" glustershd_up_status
TEST $CLI volume heal $V0
sleep 2
linkto2=$(getfattr -n trusted.glusterfs.dht.linkto $B0/r2d2_2/13 2>/dev/null | grep -v ^#)
linkto3=$(getfattr -n trusted.glusterfs.dht.linkto $B0/r2d2_3/13 2>/dev/null | grep -v ^#)
TEST [ $linkto2 = $linkto3 ]
EXPECT "0" stat -c "%s" $B0/r2d2_2/13
EXPECT "0" stat -c "%s" $B0/r2d2_3/13

#Test that mknod and create fops are marking the changelog properly
kill -9 $(cat /var/lib/glusterd/vols/$V0/run/$H0$B0_hiphenated-r2d2_2.pid)
TEST touch $M0/a
TEST mknod $M0/b b 0 0
EXPECT "trusted.afr.$V0-client-2=0x000000010000000200000000" echo $(getfattr -n trusted.afr.$V0-client-2 -e hex $B0/r2d2_3/a 2>/dev/null | grep -v ^#)
EXPECT "trusted.afr.$V0-client-2=0x000000000000000100000000" echo $(getfattr -n trusted.afr.$V0-client-2 -e hex $B0/r2d2_3/b 2>/dev/null | grep -v ^#)
cleanup;
