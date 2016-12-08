#!/bin/bash

#Test the split-brain resolution CLI commands.
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

function get_replicate_subvol_number {
        local filename=$1
        #get_backend_paths
        if [ -f $B0/${V0}1/$filename ]
        then
                echo 0
        elif [ -f $B0/${V0}3/$filename ]
        then    echo 1
        else
                echo -1
        fi
}

cleanup;

AREQUAL_PATH=$(dirname $0)/../../utils
CFLAGS=""
test "`uname -s`" != "Linux" && {
    CFLAGS="$CFLAGS -I$(dirname $0)/../../../contrib/argp-standalone ";
    CFLAGS="$CFLAGS -L$(dirname $0)/../../../contrib/argp-standalone -largp ";
    CFLAGS="$CFLAGS -lintl";
}
build_tester $AREQUAL_PATH/arequal-checksum.c $CFLAGS
TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{1,2,3,4}
TEST $CLI volume set $V0 cluster.self-heal-daemon off
TEST $CLI volume set $V0 cluster.data-self-heal off
TEST $CLI volume set $V0 cluster.metadata-self-heal off
TEST $CLI volume set $V0 cluster.entry-self-heal off
TEST $CLI volume start $V0
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0

cd $M0
for i in {1..10}
do
        echo "Initial content">>file$i
done

replica_0_files_list=(`ls $B0/${V0}1|grep -v '^\.'`)
replica_1_files_list=(`ls $B0/${V0}3|grep -v '^\.'`)

############ Create data split-brain in the files. ###########################
TEST kill_brick $V0 $H0 $B0/${V0}1
for file in ${!replica_0_files_list[*]}
do
        echo "B1 is down">>${replica_0_files_list[$file]}
done
TEST kill_brick $V0 $H0 $B0/${V0}3
for file in ${!replica_1_files_list[*]}
do
        echo "B3 is down">>${replica_1_files_list[$file]}
done

SMALLER_FILE_SIZE=$(stat -c %s file1)

TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 2

TEST kill_brick $V0 $H0 $B0/${V0}2
for file in ${!replica_0_files_list[*]}
do
        echo "B2 is down">>${replica_0_files_list[$file]}
        echo "appending more content to make it the bigger file">>${replica_0_files_list[$file]}
done
TEST kill_brick $V0 $H0 $B0/${V0}4
for file in ${!replica_1_files_list[*]}
do
        echo "B4 is down">>${replica_1_files_list[$file]}
        echo "appending more content to make it the bigger file">>${replica_1_files_list[$file]}
done

BIGGER_FILE_SIZE=$(stat -c %s file1)

TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 3


############### Acessing the files should now give EIO. ###############################
TEST ! cat file1
TEST ! cat file2
TEST ! cat file3
TEST ! cat file4
TEST ! cat file5
TEST ! cat file6
TEST ! cat file7
TEST ! cat file8
TEST ! cat file9
TEST ! cat file10
###################
TEST $CLI volume set $V0 cluster.self-heal-daemon on
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 2
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 3

################ Heal file1 using the bigger-file option  ##############
$CLI volume heal $V0 split-brain bigger-file /file1
EXPECT "0" echo $?
EXPECT $BIGGER_FILE_SIZE stat -c %s file1

################ Heal file2 using the bigger-file option and its gfid ##############
subvolume=$(get_replicate_subvol_number file2)
if [ $subvolume == 0 ]
then
        GFID=$(gf_get_gfid_xattr $B0/${V0}1/file2)
elif [ $subvolume == 1 ]
then
        GFID=$(gf_get_gfid_xattr $B0/${V0}3/file2)
fi
GFIDSTR="gfid:$(gf_gfid_xattr_to_str $GFID)"
$CLI volume heal $V0 split-brain bigger-file $GFIDSTR
EXPECT "0" echo $?

################ Heal file3 using the source-brick option  ##############
################ Use the brick having smaller file size as source #######
subvolume=$(get_replicate_subvol_number file3)
if [ $subvolume == 0 ]
then
        $CLI volume heal $V0 split-brain source-brick $H0:$B0/${V0}2 /file3
elif [ $subvolume == 1 ]
then
        $CLI volume heal $V0 split-brain source-brick $H0:$B0/${V0}4 /file3
fi
EXPECT "0" echo $?
EXPECT $SMALLER_FILE_SIZE stat -c %s file3

################ Heal file4 using the source-brick option and it's gfid ##############
################ Use the brick having smaller file size as source #######
subvolume=$(get_replicate_subvol_number file4)
if [ $subvolume == 0 ]
then
        GFID=$(gf_get_gfid_xattr $B0/${V0}1/file4)
        GFIDSTR="gfid:$(gf_gfid_xattr_to_str $GFID)"
        $CLI volume heal $V0 split-brain source-brick $H0:$B0/${V0}2 $GFIDSTR
elif [ $subvolume == 1 ]
then
        GFID=$(gf_get_gfid_xattr $B0/${V0}3/file4)
        GFIDSTR="gfid:$(gf_gfid_xattr_to_str $GFID)"
        $CLI volume heal $V0 split-brain source-brick $H0:$B0/${V0}4 $GFIDSTR
fi
EXPECT "0" echo $?
EXPECT $SMALLER_FILE_SIZE stat -c %s file4

################ Heal file5 using the latest-mtime option  ##############
subvolume=$(get_replicate_subvol_number file5)
if [ $subvolume == 0 ]
then
        mtime1=$(stat -c %Y $B0/${V0}1/file5)
        mtime2=$(stat -c %Y $B0/${V0}2/file5)
        LATEST_MTIME=$(($mtime1 > $mtime2 ? $mtime1:$mtime2))
elif [ $subvolume == 1 ]
then
        mtime1=$(stat -c %Y $B0/${V0}3/file5)
        mtime2=$(stat -c %Y $B0/${V0}4/file5)
        LATEST_MTIME=$(($mtime1 > $mtime2 ? $mtime1:$mtime2))
fi
$CLI volume heal $V0 split-brain latest-mtime /file5
EXPECT "0" echo $?

#TODO: Uncomment the below after posix_do_utimes() supports utimensat(2) accuracy
#TEST [ $LATEST_MTIME -eq $mtime1 ]
#TEST [ $LATEST_MTIME -eq $mtime2 ]

################ Heal file6 using the latest-mtime option and its gfid  ##############
subvolume=$(get_replicate_subvol_number file6)
if [ $subvolume == 0 ]
then
        GFID=$(gf_get_gfid_xattr $B0/${V0}1/file6)
        mtime1=$(stat -c %Y $B0/${V0}1/file6)
        mtime2=$(stat -c %Y $B0/${V0}2/file6)
        LATEST_MTIME=$(($mtime1 > $mtime2 ? $mtime1:$mtime2))
elif [ $subvolume == 1 ]
then
        GFID=$(gf_get_gfid_xattr $B0/${V0}3/file6)
        mtime1=$(stat -c %Y $B0/${V0}3/file6)
        mtime2=$(stat -c %Y $B0/${V0}4/file6)
        LATEST_MTIME=$(($mtime1 > $mtime2 ? $mtime1:$mtime2))
fi
GFIDSTR="gfid:$(gf_gfid_xattr_to_str $GFID)"
$CLI volume heal $V0 split-brain latest-mtime $GFIDSTR
EXPECT "0" echo $?

#TODO: Uncomment the below after posix_do_utimes() supports utimensat(2) accuracy
#TEST [ $LATEST_MTIME -eq $mtime1 ]
#TEST [ $LATEST_MTIME -eq $mtime2 ]

################ Heal remaining SB'ed files of replica_0 using B1 as source ##############
$CLI volume heal $V0 split-brain source-brick $H0:$B0/${V0}1
EXPECT "0" echo $?

################ Heal remaining SB'ed files of replica_1 using B3 as source ##############
$CLI volume heal $V0 split-brain source-brick $H0:$B0/${V0}3
EXPECT "0" echo $?

############### Reading the files should now succeed. ###############################
TEST  cat file1
TEST  cat file2
TEST  cat file3
TEST  cat file4
TEST  cat file5
TEST  cat file6
TEST  cat file7
TEST  cat file8
TEST  cat file9
TEST  cat file10

################ File contents on the bricks must be same. ################################
TEST diff <(arequal-checksum -p $B0/$V01 -i .glusterfs) <(arequal-checksum -p $B0/$V02 -i .glusterfs)
TEST diff <(arequal-checksum -p $B0/$V03 -i .glusterfs) <(arequal-checksum -p $B0/$V04 -i .glusterfs)

############### Trying to heal files not in SB should fail. ###############################
$CLI volume heal $V0 split-brain bigger-file /file1
EXPECT "1" echo $?
$CLI volume heal $V0 split-brain source-brick $H0:$B0/${V0}4 /file3
EXPECT "1" echo $?

cd -
TEST rm $AREQUAL_PATH/arequal-checksum
cleanup
