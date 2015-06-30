#!/bin/bash
#Self-heal tests

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
cleanup;

#Init
AREQUAL_PATH=$(dirname $0)/../../utils
AREQUAL_BIN=$AREQUAL_PATH/arequal-checksum
CFLAGS=""
test "`uname -s`" != "Linux" && {
    CFLAGS="$CFLAGS -I$(dirname $0)/../../../contrib/argp-standalone ";
    CFLAGS="$CFLAGS -L$(dirname $0)/../../../contrib/argp-standalone -largp ";
    CFLAGS="$CFLAGS -lintl";
}
build_tester $AREQUAL_PATH/arequal-checksum.c $CFLAGS
TEST [ -e $AREQUAL_BIN ]

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/brick{0,1}
TEST $CLI volume set $V0 stat-prefetch off
TEST $CLI volume start $V0
TEST $CLI volume set $V0 cluster.background-self-heal-count 0
TEST $GFS --volfile-id=$V0 --volfile-server=$H0 $M0;

###############################################################################
#1.Test successful data, metadata and entry self-heal

#Test
TEST mkdir -p $M0/abc/def $M0/abc/ghi
TEST dd if=/dev/urandom of=$M0/abc/file_abc.txt bs=1024k count=2 2>/dev/null
TEST dd if=/dev/urandom of=$M0/abc/def/file_abc_def_1.txt bs=1024k count=2 2>/dev/null
TEST dd if=/dev/urandom of=$M0/abc/def/file_abc_def_2.txt bs=1024k count=3 2>/dev/null
TEST dd if=/dev/urandom of=$M0/abc/ghi/file_abc_ghi.txt bs=1024k count=4 2>/dev/null

TEST kill_brick $V0 $H0 $B0/brick0
TEST truncate -s 0 $M0/abc/def/file_abc_def_1.txt
NEW_UID=36
NEW_GID=36
TEST chown  $NEW_UID:$NEW_GID $M0/abc/def/file_abc_def_2.txt
TEST rm -rf $M0/abc/ghi
TEST mkdir -p $M0/def/ghi $M0/jkl/mno
TEST dd if=/dev/urandom of=$M0/def/ghi/file1.txt bs=1024k count=2 2>/dev/null
TEST dd if=/dev/urandom of=$M0/def/ghi/file2.txt bs=1024k count=3 2>/dev/null
TEST dd if=/dev/urandom of=$M0/jkl/mno/file.txt bs=1024k count=4 2>/dev/null
TEST chown  $NEW_UID:$NEW_GID $M0/def/ghi/file2.txt

TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "0" get_pending_heal_count $V0

#check all files created/deleted on brick1 are also replicated on brick 0
#(i.e. no reverse heal has happened)
TEST ls $B0/brick0/def/ghi/file1.txt
TEST ls $B0/brick0/def/ghi/file2.txt
TEST ls $B0/brick0/jkl/mno/file.txt
TEST ! ls $B0/brick0/abc/ghi
EXPECT "$NEW_UID$NEW_GID" stat -c %u%g $B0/brick0/abc/def/file_abc_def_2.txt
TEST diff <($AREQUAL_BIN -p $B0/brick0 -i .glusterfs) <($AREQUAL_BIN -p $B0/brick1 -i .glusterfs)

#Cleanup
TEST rm -rf $M0/*
###############################################################################

#2.Test successful self-heal of different file types.

#Test
TEST touch $M0/file
TEST kill_brick $V0 $H0 $B0/brick0
TEST rm -f $M0/file
TEST mkdir $M0/file

TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "0" get_pending_heal_count $V0

#check heal has happened in the correct direction
TEST test -d $B0/brick0/file
TEST diff <($AREQUAL_BIN -p $B0/brick0 -i .glusterfs) <($AREQUAL_BIN -p $B0/brick1 -i .glusterfs)

#Cleanup
TEST rm -rf $M0/*
###############################################################################

#3.Test successful self-heal of file permissions.

#Test
TEST touch $M0/file
TEST chmod 666 $M0/file
TEST kill_brick $V0 $H0 $B0/brick0
TEST chmod 777 $M0/file
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "0" get_pending_heal_count $V0

#check heal has happened in the correct direction
EXPECT "777" stat -c %a $B0/brick0/file
TEST diff <($AREQUAL_BIN -p $B0/brick0 -i .glusterfs) <($AREQUAL_BIN -p $B0/brick1 -i .glusterfs)

#Cleanup
TEST rm -rf $M0/*
###############################################################################

#4.Test successful self-heal of file ownership

#Test
TEST touch $M0/file
TEST kill_brick $V0 $H0 $B0/brick0
NEW_UID=36
NEW_GID=36
TEST chown $NEW_UID:$NEW_GID $M0/file
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "0" get_pending_heal_count $V0

#check heal has happened in the correct direction
EXPECT "$NEW_UID$NEW_GID" stat -c %u%g $B0/brick0/file
TEST diff <($AREQUAL_BIN -p $B0/brick0 -i .glusterfs) <($AREQUAL_BIN -p $B0/brick1 -i .glusterfs)

#Cleanup
TEST rm -rf $M0/*
###############################################################################

#5.File size test

#Test
TEST touch $M0/file
TEST `echo "write1">$M0/file`
TEST kill_brick $V0 $H0 $B0/brick0
TEST `echo "write2">>$M0/file`
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
TEST $CLI volume heal $V0
TEST kill_brick $V0 $H0 $B0/brick1
TEST truncate -s 0 $M0/file
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "0" get_pending_heal_count $V0

#check heal has happened in the correct direction
EXPECT 0 stat -c %s $B0/brick1/file
TEST diff <($AREQUAL_BIN -p $B0/brick0 -i .glusterfs) <($AREQUAL_BIN -p $B0/brick1 -i .glusterfs)

#Cleanup
TEST rm -rf $M0/*
###############################################################################

#6.GFID heal

#Test
TEST touch $M0/file
TEST kill_brick $V0 $H0 $B0/brick0
TEST rm -f $M0/file
TEST touch $M0/file
GFID=$(gf_get_gfid_xattr $B0/brick1/file)
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "0" get_pending_heal_count $V0

#check heal has happened in the correct direction
EXPECT "$GFID" gf_get_gfid_xattr $B0/brick0/file

#Cleanup
TEST rm -rf $M0/*
###############################################################################

#7. Link/symlink heal

#Test
TEST touch $M0/file
TEST ln $M0/file $M0/link_to_file
TEST kill_brick $V0 $H0 $B0/brick0
TEST rm -f $M0/link_to_file
TEST ln -s $M0/file $M0/link_to_file
TEST ln  $M0/file $M0/hard_link_to_file
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "0" get_pending_heal_count $V0

#check heal has happened in the correct direction
TEST test -f $B0/brick0/hard_link_to_file
TEST test -h $B0/brick0/link_to_file
TEST diff <($AREQUAL_BIN -p $B0/brick0 -i .glusterfs) <($AREQUAL_BIN -p $B0/brick1 -i .glusterfs)

#Cleanup
TEST rm -rf $M0/*
###############################################################################

#8. Heal xattrs set by application

#Test
TEST touch $M0/file
TEST setfattr -n user.myattr_1 -v My_attribute_1 $M0/file
TEST setfattr -n user.myattr_2 -v "My_attribute_2" $M0/file
TEST kill_brick $V0 $H0 $B0/brick0
TEST setfattr -n user.myattr_1 -v "My_attribute_1_modified" $M0/file
TEST setfattr -n user.myattr_3 -v "My_attribute_3" $M0/file
TEST $CLI volume start $V0 force
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "0" get_pending_heal_count $V0

TEST diff <(echo "user.myattr_1=\"My_attribute_1_modified\"") <(getfattr -n user.myattr_1 $B0/brick1/file|grep user.myattr_1)
TEST diff <(echo "user.myattr_3=\"My_attribute_3\"") <(getfattr -n user.myattr_3 $B0/brick1/file|grep user.myattr_3)

#Cleanup
TEST rm -rf $M0/*
###############################################################################

TEST rm -rf $AREQUAL_BIN
cleanup;
