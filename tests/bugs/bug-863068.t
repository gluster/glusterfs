#!/bin/bash

. $(dirname $0)/../include.rc

cleanup;

## This function get the No. of entries for
## gluster volume heal volnmae info healed command for brick1 and brick2
## and compare the initial value (Before volume heal full) and final value
## (After gluster volume heal vol full) and compare.

function getdiff()
{
	val=10
	if [ "$1" == "$3" ]
	then
		if [ "$2" == "$4" ]
			then
				val=0
		else
				val=20
		fi
      fi

      echo $val
}


TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;
TEST $CLI volume create $V0 replica 2  $H0:$B0/brick1 $H0:$B0/brick2;
TEST $CLI volume start $V0;
TEST glusterfs --volfile-server=$H0 --volfile-id=$V0 $M0;
B0_hiphenated=`echo $B0 | tr '/' '-'`
kill -9 `cat /var/lib/glusterd/vols/$V0/run/$H0$B0_hiphenated-brick1.pid` ;

mkdir $M0/{a,b,c};
echo "GLUSTERFS" >> $M0/a/file;

TEST $CLI volume start $V0 force;
sleep 5
TEST $CLI volume heal $V0 full;
sleep 5

##First Brick Initial(Before full type self heal) value
FBI=`gluster volume heal $V0 info healed | grep entries | awk '{print $4}' | head -n 1`

##Second Brick Initial Value
SBI=`gluster volume heal $V0 info healed | grep entries | awk '{print $4}' | tail -n 1`
TEST $CLI volume heal $V0 full;

sleep 5

##First Brick Final value
##Number of entries from output of <gluster volume heal volname info healed>

FBF=`gluster volume heal $V0 info healed | grep entries | awk '{print $4}' | head -n 1`

##Second Brick Final Value
SBF=`gluster volume heal $V0 info healed | grep entries | awk '{print $4}' | tail -n 1`

##get the difference of values
EXPECT "0"  getdiff $FBI $SBI $FBF $SBF;

## Tests after this comment checks for the background self heal

TEST mkdir $M0/d
kill -9 `cat /var/lib/glusterd/vols/$V0/run/$H0$B0_hiphenated-brick1.pid` ;
TEST $CLI volume set $V0 self-heal-daemon off
dd if=/dev/random of=$M0/d/file1 bs=100M count=1 2>/dev/null;
TEST $CLI volume start $V0 force
sleep 3
TEST ls -l $M0/d

cleanup;
