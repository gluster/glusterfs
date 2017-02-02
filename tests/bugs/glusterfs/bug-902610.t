#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

## Layout-spread set to 3, but subvols up are 2. So layout should split 50-50
function get_layout()
{
        layout1=`getfattr -n trusted.glusterfs.dht -e hex $1 2>&1|grep dht |cut -d = -f2`
	layout1_s=$(echo $layout1 | cut -c 19-26)
	layout1_e=$(echo $layout1 | cut -c 27-34)
	#echo "layout1 from $layout1_s to $layout1_e" > /dev/tty
        layout2=`getfattr -n trusted.glusterfs.dht -e hex $2 2>&1|grep dht |cut -d = -f2`
	layout2_s=$(echo $layout2 | cut -c 19-26)
	layout2_e=$(echo $layout2 | cut -c 27-34)
	#echo "layout2 from $layout2_s to $layout2_e" > /dev/tty

	if [ x"$layout2_s" = x"00000000" ]; then
		# Reverse so we only have the real logic in one place.
		tmp_s=$layout1_s
		tmp_e=$layout1_e
		layout1_s=$layout2_s
		layout1_e=$layout2_e
		layout2_s=$tmp_s
		layout2_e=$tmp_e
	fi

	# Figure out where the join point is.
	target=$( $PYTHON -c "print '%08x' % (0x$layout1_e + 1)")
	#echo "target for layout2 = $target" > /dev/tty

	# The second layout should cover everything that the first doesn't.
	if [ x"$layout2_s" = x"$target" -a x"$layout2_e" = x"ffffffff" ]; then
		return 0
	fi

	return 1
}

BRICK_COUNT=4

TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/${V0}0 $H0:$B0/${V0}1 $H0:$B0/${V0}2 $H0:$B0/${V0}3
## set subvols-per-dir option
TEST $CLI volume set $V0 subvols-per-directory 3
TEST $CLI volume start $V0

## Mount FUSE
TEST glusterfs -s $H0 --volfile-id $V0 $M0 --entry-timeout=0 --attribute-timeout=0;

TEST ls -l $M0

## kill 2 bricks to bring down available subvol < spread count
kill_brick $V0 $H0 $B0/${V0}2
kill_brick $V0 $H0 $B0/${V0}3

mkdir $M0/dir1 2>/dev/null

get_layout $B0/${V0}0/dir1 $B0/${V0}1/dir1
EXPECT "0" echo $?

cleanup;
