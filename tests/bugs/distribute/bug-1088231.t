#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../fileio.rc
. $(dirname $0)/../../dht.rc


cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0  $H0:$B0/${V0}{0,1}
TEST $CLI volume set $V0 cluster.randomize-hash-range-by-gfid on
TEST $CLI volume start $V0
TEST glusterfs --volfile-id=/$V0 --aux-gfid-mount --volfile-server=$H0 $M0
TEST mkdir $M0/a


## Bug Description: In case of dht_discover code path, which is triggered
## when lookup done is nameless lookup, at the end of the lookup, even if
## it finds that self-heal is needed to fix-the layout it wont heal because
## healing code path is not added under nameless lookup.

## What to test: With Patch, Even in case of nameless lookup, if layout
## needs to be fixed,  the it will be fixed wherever lookup is successful
## and it will not create any directory for subvols having ENOENT as it is
## nameless lookup.

gfid_with_hyphen=`getfattr -n glusterfs.gfid.string $M0/a 2>/dev/null \
                  | grep glusterfs.gfid.string | cut -d '"' -f 2`

TEST setfattr -x trusted.glusterfs.dht $B0/$V0"0"/a

## new healing code don't attempt healing if inode is already
## populated. So, unmount and remount before we do stat.
TEST umount $M0
TEST glusterfs --volfile-id=/$V0 --aux-gfid-mount --volfile-server=$H0 $M0

TEST stat $M0/.gfid/$gfid_with_hyphen

##  Assuming that we have two bricks, we can have two permutations of layout
##   Case 1:  Brick - A               Brick -  B
##              0 - 50                   51-100
##
##   Case 2:  Brick - A               Brick -  B
##             51 - 100                   0 - 50
##
##  To ensure layout is assigned properly, the following tests should be
##  performed.
##
##   Case 1:    Layout_b0_s = 0; Layout_b0_e = 50, Layout_b1_s=51,
##              Layout_b1_e = 100;
##
##              layout_b1_s = layout_b0_e + 1;
##              layout_b0_s = layout_b1_e + 1; but b0_s is 0, so change to 101
##                                             then compare
##  Case 2:     Layout_b0_s = 51, Layout_b0_e = 100, Layout_b1_s=0,
##              Layout_b1_e = 51
##
##             layout_b0_s  = Layout_b1_e + 1;
##             layout_b1_s  = Layout_b0_e + 1; but b1_s is 0, so chage to 101.


##Extract Layout
echo `get_layout  $B0/$V0"0"/a`
echo `get_layout  $B0/$V0"1"/a`
layout_b0_s=`get_layout $B0/$V0"0"/a  | cut -c19-26`
layout_b0_e=`get_layout $B0/$V0"0"/a  | cut -c27-34`
layout_b1_s=`get_layout $B0/$V0"1"/a  | cut -c19-26`
layout_b1_e=`get_layout $B0/$V0"1"/a  | cut -c27-34`


##Add 0X to perform Hex arithematic
layout_b0_s="0x"$layout_b0_s
layout_b0_e="0x"$layout_b0_e
layout_b1_s="0x"$layout_b1_s
layout_b1_e="0x"$layout_b1_e


## Logic of converting starting layout "0" to "Max_value of layout + 1"
comp1=$(($layout_b0_s + 0))
if [ "$comp1" == "0" ];then
	comp1=4294967296
fi

comp2=$(($layout_b1_s + 0))
if [ "$comp2" == "0" ];then
	comp2=4294967296
fi

diff1=$(($layout_b0_e + 1))
diff2=$(($layout_b1_e + 1))


healed=0

if [ "$comp1" == "$diff1" ] && [ "$comp2" == "$diff2" ]; then
   healed=$(($healed + 1))
fi

if [ "$comp1" == "$diff2" ] && [ "$comp2" == "$diff1" ]; then
	healed=$(($healed + 1))
fi

TEST [ $healed == 1 ]

cleanup

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0  $H0:$B0/${V0}{0,1}
TEST $CLI volume set $V0 cluster.randomize-hash-range-by-gfid on
TEST $CLI volume start $V0
TEST glusterfs --volfile-id=/$V0 --aux-gfid-mount --volfile-server=$H0 $M0
TEST mkdir $M0/a

gfid_with_hyphen=`getfattr -n glusterfs.gfid.string $M0/a 2>/dev/null \
                  | grep glusterfs.gfid.string | cut -d '"' -f 2`

TEST setfattr -x trusted.glusterfs.dht $B0/$V0"0"/a
TEST setfattr -x trusted.glusterfs.dht $B0/$V0"1"/a

## new healing code don't attempt healing if inode is already
## populated. So, unmount and remount before we do stat.
TEST umount $M0
TEST glusterfs --volfile-id=/$V0 --aux-gfid-mount --volfile-server=$H0 $M0

TEST stat $M0/.gfid/$gfid_with_hyphen

##Extract Layout

layout_b0_s=`get_layout $B0/$V0"0"/a  | cut -c19-26`
layout_b0_e=`get_layout $B0/$V0"0"/a  | cut -c27-34`
layout_b1_s=`get_layout $B0/$V0"1"/a  | cut -c19-26`
layout_b1_e=`get_layout $B0/$V0"1"/a  | cut -c27-34`


##Add 0X to perform Hex arithematic
layout_b0_s="0x"$layout_b0_s
layout_b0_e="0x"$layout_b0_e
layout_b1_s="0x"$layout_b1_s
layout_b1_e="0x"$layout_b1_e



## Logic of converting starting layout "0" to "Max_value of layout + 1"
comp1=$(($layout_b0_s + 0))
if [ "$comp1" == "0" ];then
        comp1=4294967296
fi

comp2=$(($layout_b1_s + 0))
if [ "$comp2" == "0" ];then
        comp2=4294967296
fi

diff1=$(($layout_b0_e + 1))
diff2=$(($layout_b1_e + 1))


healed=0

if [ "$comp1" == "$diff1" ] && [ "$comp2" == "$diff2" ]; then
   healed=$(($healed + 1))
fi

if [ "$comp1" == "$diff2" ] && [ "$comp2" == "$diff1" ]; then
        healed=$(($healed + 1))
fi

TEST [ $healed == 1 ]
cleanup

