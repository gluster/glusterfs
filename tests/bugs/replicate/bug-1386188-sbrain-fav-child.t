#!/bin/bash
. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume set $V0 self-heal-daemon off
TEST $CLI volume set $V0 data-self-heal off
TEST $CLI volume set $V0 entry-self-heal off
TEST $CLI volume set $V0 metadata-self-heal off
TEST $CLI volume start $V0

TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0;
TEST touch $M0/data.txt
TEST touch $M0/mdata.txt

#Create data and metadata split-brain
TEST kill_brick $V0 $H0 $B0/${V0}0
TEST dd if=/dev/urandom of=$M0/data.txt bs=1024 count=1024
TEST setfattr -n user.value -v value1 $M0/mdata.txt
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 0
TEST kill_brick $V0 $H0 $B0/${V0}1
TEST dd if=/dev/urandom of=$M0/data.txt bs=1024 count=1024
TEST setfattr -n user.value -v value2 $M0/mdata.txt

TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}1
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" afr_child_up_status $V0 1

## Check that the file still in split-brain,
  ## I/O fails
  cat $M0/data.txt > /dev/null
  EXPECT "1" echo $?
  ## pending xattrs blame each other.
  brick0_pending=$(get_hex_xattr trusted.afr.$V0-client-1 $B0/${V0}0/data.txt)
  brick1_pending=$(get_hex_xattr trusted.afr.$V0-client-0 $B0/${V0}1/data.txt)
  TEST [ $brick0_pending -ne "000000000000000000000000" ]
  TEST [ $brick1_pending -ne "000000000000000000000000" ]

  ## I/O fails
  getfattr -n user.value $M0/mdata.txt
  EXPECT "1" echo $?
  brick0_pending=$(get_hex_xattr trusted.afr.$V0-client-1 $B0/${V0}0/mdata.txt)
  brick1_pending=$(get_hex_xattr trusted.afr.$V0-client-0 $B0/${V0}1/mdata.txt)
  TEST [ $brick0_pending -ne "000000000000000000000000" ]
  TEST [ $brick1_pending -ne "000000000000000000000000" ]

## Let us use mtime as fav-child policy. So brick0 will be source.
   # Set dirty (data part) on the sink brick to check if it is reset later along with the pending xattr.
   TEST setfattr -n trusted.afr.dirty -v 0x000000010000000000000000 $B0/${V0}1/data.txt
   # Set dirty (metadata part) on the sink brick to check if it is reset later along with the pending xattr.
   TEST setfattr -n trusted.afr.dirty -v 0x000000000000000100000000 $B0/${V0}1/mdata.txt

   TEST $CLI volume set $V0 favorite-child-policy mtime

   # Reading the file should be allowed and sink brick xattrs must be reset.
   cat $M0/data.txt > /dev/null
   EXPECT "0" echo $?
   TEST brick1_pending=$(get_hex_xattr trusted.afr.$V0-client-0 $B0/${V0}1/data.txt)
   TEST brick1_dirty=$(get_hex_xattr trusted.afr.dirty $B0/${V0}1/data.txt)
   TEST [ $brick1_dirty -eq "000000000000000000000000" ]
   TEST [ $brick1_pending -eq "000000000000000000000000" ]

   # Accessing the file should be allowed and sink brick xattrs must be reset.
   EXPECT "value2" echo $(getfattr --only-values -n user.value  $M0/mdata.txt)
   TEST brick1_pending=$(get_hex_xattr trusted.afr.$V0-client-0 $B0/${V0}1/data.txt)
   TEST brick1_dirty=$(get_hex_xattr trusted.afr.dirty $B0/${V0}1/data.txt)
   TEST [ $brick1_dirty -eq "000000000000000000000000" ]
   TEST [ $brick1_pending -eq "000000000000000000000000" ]

#Enable shd and heal the file.
TEST $CLI volume set $V0 cluster.self-heal-daemon on
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
TEST $CLI volume heal $V0
EXPECT 0 get_pending_heal_count $V0
cleanup;
