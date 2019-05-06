#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

#This .t tests the functionality of open-fd-heal when opened with O_TRUNC.
#If re-open is not done with O_TRUNC then the test will pass.

cleanup

TEST glusterd

TEST $CLI volume create $V0 disperse 3 ${H0}:$B0/brick{1,2,3}
EXPECT 'Created' volinfo_field $V0 'Status'
#Disable heals to prevent any chance of heals masking the problem
TEST $CLI volume set $V0 disperse.background-heals 0
TEST $CLI volume set $V0 disperse.heal-wait-qlength 0
TEST $CLI volume set $V0 performance.write-behind off

#We need truncate fop to go through before pre-op completes for the write-fop
#which triggers open-fd heal. Otherwise truncate won't be allowed on 'bad' brick
TEST $CLI volume set $V0 delay-gen posix
TEST $CLI volume set $V0 delay-gen.enable fxattrop
TEST $CLI volume set $V0 delay-gen.delay-percentage 100
TEST $CLI volume set $V0 delay-gen.delay-duration 1000000

TEST $CLI volume heal $V0 disable

TEST $CLI volume start $V0
TEST $CLI volume profile $V0 start
EXPECT 'Started' volinfo_field $V0 'Status'
TEST glusterfs --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id $V0 $M0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "3" ec_child_up_count $V0 0
TEST touch $M0/a
EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0

TEST kill_brick $V0 $H0 $B0/brick1
logdir=`gluster --print-logdir`

TEST build_tester $(dirname $0)/gfapi-ec-open-truncate.c -lgfapi

TEST $CLI volume profile $V0 info clear
TEST ./$(dirname $0)/gfapi-ec-open-truncate ${H0} $V0 $logdir/gfapi-ec-open-truncate.log

EXPECT "^2$" echo $($CLI volume profile $V0 info incremental | grep -i truncate | wc -l)
cleanup_tester $(dirname $0)/gfapi-ec-open-truncate

cleanup
