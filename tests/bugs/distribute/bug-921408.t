#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../fileio.rc
. $(dirname $0)/../../dht.rc
. $(dirname $0)/../../volume.rc

cleanup;
wait_check_status ()
{
        n=0
        while [ $n -lt $1 ]
        do
                ret=$(rebalance_completed)
                if [ $ret == "0" ]
                then
                        return 0;
                else
                        sleep 1
                        n=`expr $n + 1`;
                fi
       done
       return 1;
}

addbr_rebal_till_layout_change()
{
        val=1
        l=$1
        i=1
        while [ $i -lt 5 ]
        do
                $CLI volume add-brick $V0 $H0:$B0/${V0}$l &>/dev/null
                $CLI volume rebalance $V0 fix-layout start &>/dev/null
                wait_check_status $REBALANCE_TIMEOUT
                if [ $? -eq 1 ]
                then
                        break
                fi
                NEW_LAYOUT=`get_layout $B0/${V0}0 | cut -c11-34`
                if [ $OLD_LAYOUT == $NEW_LAYOUT ]
                then
                        i=`expr $i + 1`;
                        l=`expr $l + 1`;
                else
                        val=0
                        break
                fi
        done
        return $val
}
TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/${V0}0
TEST $CLI volume set $V0 subvols-per-directory 1
TEST $CLI volume start $V0

TEST glusterfs -s $H0 --volfile-id $V0 $M0;

TEST mkdir $M0/test
TEST touch $M0/test/test

fd=`fd_available`
TEST fd_open $fd "rw" $M0/test/test

OLD_LAYOUT=`get_layout $B0/${V0}0 | cut -c11-34`

addbr_rebal_till_layout_change 1

TEST [ $? -eq 0 ]

for i in $(seq 1 1000)
do
	ls -l $M0/ >/dev/null
	ret=$?
	if [ $ret != 0 ]
	then
		break
	fi
done

TEST [ $ret == 0 ];
TEST fd_close $fd;

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

cleanup
