#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../fileio.rc
. $(dirname $0)/../../dht.rc
cleanup;

## Start and create a volume
TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume create $V0 $H0:$B0/${V0}{1,2}
TEST $CLI volume start $V0;
TEST glusterfs --direct-io-mode=yes --entry-timeout=0 --attribute-timeout=0 -s $H0 --volfile-id $V0 $M0;

echo "D" > $M0/file1;
TEST chmod +st $M0/file1;

TEST $CLI volume add-brick $V0 $H0:$B0/${V0}"3"
TEST $CLI volume rebalance $V0 start force

EXPECT_WITHIN "10" "0" rebalance_completed
count=0
for i in `ls $B0/$V0"3"`;
	do
		var=`stat -c %A $B0/$V0"3"/$i | cut -c 4`;
		echo $B0/$V0"3"/$i $var
		if [ "$var" != "S" ]; then
			count=$((count + 1))
		fi
	done

TEST [[ $count == 0 ]]
cleanup
