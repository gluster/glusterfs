#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../afr.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;
TEST $CLI volume create $V0 replica 2  $H0:$B0/brick1 $H0:$B0/brick2;
TEST $CLI volume start $V0;


TEST glusterfs --volfile-server=$H0 --volfile-id=$V0 $M0;
B0_hiphenated=`echo $B0 | tr '/' '-'`
kill_brick $V0 $H0 $B0/brick1

echo "GLUSTER FILE SYSTEM" > $M0/FILE1
echo "GLUSTER FILE SYSTEM" > $M0/FILE2

FILEN=$B0"/brick2"
XATTROP=$FILEN/.glusterfs/indices/xattrop

function get_gfid()
{
path_of_file=$1

gfid_value=`getfattr -d -m . $path_of_file -e hex 2>/dev/null |  grep trusted.gfid | cut --complement -c -15 | sed 's/\([a-f0-9]\{8\}\)\([a-f0-9]\{4\}\)\([a-f0-9]\{4\}\)\([a-f0-9]\{4\}\)/\1-\2-\3-\4-/'`

echo $gfid_value
}

GFID_ROOT=`get_gfid $B0/brick2`
GFID_FILE1=`get_gfid $B0/brick2/FILE1`
GFID_FILE2=`get_gfid $B0/brick2/FILE2`


count=0
for i in `ls $XATTROP`
do
 if [ "$i" == "$GFID_ROOT" ] || [ "$i" == "$GFID_FILE1" ] || [ "$i" == "$GFID_FILE2" ]
        then
 count=$(( count + 1 ))
 fi
done

EXPECT "3" echo $count


TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
TEST $CLI volume heal $V0


##Expected number of entries are 0 in the .glusterfs/indices/xattrop directory
EXPECT_WITHIN $HEAL_TIMEOUT '0' count_sh_entries $FILEN;

TEST $CLI volume stop $V0;
TEST $CLI volume delete $V0;

cleanup;
