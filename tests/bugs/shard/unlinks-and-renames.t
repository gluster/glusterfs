#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup

# The aim of this test script is to exercise the various codepaths of unlink
# and rename fops in sharding and make sure they work fine.
#

#################################################
################### UNLINK ######################
#################################################

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume set $V0 features.shard on
TEST $CLI volume start $V0
TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0

TEST mkdir $M0/dir
TEST touch $M0/dir/foo
TEST touch $M0/dir/new

######################################
##### Unlink with /.shard absent #####
######################################
TEST truncate -s 5M $M0/dir/foo
TEST ! stat $B0/${V0}0/.shard
TEST ! stat $B0/${V0}1/.shard
# Test to ensure that unlink doesn't fail due to absence of /.shard
TEST unlink $M0/dir/foo

##################################################
##### Unlink of a sharded file without holes #####
##################################################
# Create a 9M sharded file
TEST dd if=/dev/zero of=$M0/dir/new bs=1024 count=9216
gfid_new=$(get_gfid_string $M0/dir/new)
# Ensure its shards are created.
TEST stat $B0/${V0}0/.shard/$gfid_new.1
TEST stat $B0/${V0}1/.shard/$gfid_new.1
TEST stat $B0/${V0}0/.shard/$gfid_new.2
TEST stat $B0/${V0}1/.shard/$gfid_new.2
TEST unlink $M0/dir/new
TEST ! stat $B0/${V0}0/.shard/$gfid_new.1
TEST ! stat $B0/${V0}1/.shard/$gfid_new.1
TEST ! stat $B0/${V0}0/.shard/$gfid_new.2
TEST ! stat $B0/${V0}1/.shard/$gfid_new.2
TEST ! stat $M0/dir/new
TEST ! stat $B0/${V0}0/dir/new
TEST ! stat $B0/${V0}1/dir/new

#######################################
##### Unlink with /.shard present #####
#######################################
TEST truncate -s 5M $M0/dir/foo
gfid_foo=$(get_gfid_string $M0/dir/foo)
# Ensure its shards are absent.
TEST ! stat $B0/${V0}0/.shard/$gfid_foo.1
TEST ! stat $B0/${V0}1/.shard/$gfid_foo.1
# Test to ensure that unlink of a sparse file works fine.
TEST unlink $M0/dir/foo
TEST ! stat $B0/${V0}0/dir/foo
TEST ! stat $B0/${V0}1/dir/foo
TEST ! stat $M0/dir/foo

#############################################################
##### Unlink of a file with only one block (the zeroth) #####
#############################################################
TEST touch $M0/dir/foo
TEST dd if=/dev/zero of=$M0/dir/foo bs=1024 count=1024
# Test to ensure that unlink of a sparse file works fine.
TEST unlink $M0/dir/foo
TEST ! stat $B0/${V0}0/dir/foo
TEST ! stat $B0/${V0}1/dir/foo
TEST ! stat $M0/dir/foo

####################################################
##### Unlink of a sharded file with hard-links #####
####################################################
# Create a 9M sharded file
TEST dd if=/dev/zero of=$M0/dir/original bs=1024 count=9216
gfid_original=$(get_gfid_string $M0/dir/original)
# Ensure its shards are created.
TEST stat $B0/${V0}0/.shard/$gfid_original.1
TEST stat $B0/${V0}1/.shard/$gfid_original.1
TEST stat $B0/${V0}0/.shard/$gfid_original.2
TEST stat $B0/${V0}1/.shard/$gfid_original.2
# Create a hard link.
TEST ln $M0/dir/original $M0/link
# Now delete the original file.
TEST unlink $M0/dir/original
# Ensure the shards are still intact.
TEST stat $B0/${V0}0/.shard/$gfid_original.1
TEST stat $B0/${V0}1/.shard/$gfid_original.1
TEST stat $B0/${V0}0/.shard/$gfid_original.2
TEST stat $B0/${V0}1/.shard/$gfid_original.2
TEST ! stat $M0/dir/original
TEST stat $M0/link
TEST stat $B0/${V0}0/link
TEST stat $B0/${V0}1/link
# Now delete the last link.
TEST unlink $M0/link
# Ensure that the shards are all cleaned up.
TEST ! stat $B0/${V0}0/.shard/$gfid_original.1
TEST ! stat $B0/${V0}1/.shard/$gfid_original.1
TEST ! stat $B0/${V0}0/.shard/$gfid_original.2
TEST ! stat $B0/${V0}1/.shard/$gfid_original.2
TEST ! stat $M0/link
TEST ! stat $B0/${V0}0/link
TEST ! stat $B0/${V0}1/link

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $M0
TEST $CLI volume stop $V0
TEST $CLI volume delete $V0

cleanup

#################################################
################### RENAME ######################
#################################################

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume set $V0 features.shard on
TEST $CLI volume start $V0
TEST glusterfs --volfile-id=$V0 --volfile-server=$H0 $M0

TEST mkdir $M0/dir
TEST touch $M0/dir/src
TEST touch $M0/dir/dst

######################################
##### Rename with /.shard absent #####
######################################
TEST truncate -s 5M $M0/dir/dst
TEST ! stat $B0/${V0}0/.shard
TEST ! stat $B0/${V0}1/.shard
# Test to ensure that rename doesn't fail due to absence of /.shard
TEST mv -f $M0/dir/src $M0/dir/dst
TEST ! stat $M0/dir/src
TEST   stat $M0/dir/dst
TEST ! stat $B0/${V0}0/dir/src
TEST ! stat $B0/${V0}1/dir/src
TEST   stat $B0/${V0}0/dir/dst
TEST   stat $B0/${V0}1/dir/dst

##################################################
##### Rename to a sharded file without holes #####
##################################################
TEST unlink $M0/dir/dst
TEST touch $M0/dir/src
# Create a 9M sharded file
TEST dd if=/dev/zero of=$M0/dir/dst bs=1024 count=9216
gfid_dst=$(get_gfid_string $M0/dir/dst)
# Ensure its shards are created.
TEST stat $B0/${V0}0/.shard/$gfid_dst.1
TEST stat $B0/${V0}1/.shard/$gfid_dst.1
TEST stat $B0/${V0}0/.shard/$gfid_dst.2
TEST stat $B0/${V0}1/.shard/$gfid_dst.2
TEST mv -f $M0/dir/src $M0/dir/dst
TEST ! stat $B0/${V0}0/.shard/$gfid_dst.1
TEST ! stat $B0/${V0}1/.shard/$gfid_dst.1
TEST ! stat $B0/${V0}0/.shard/$gfid_dst.2
TEST ! stat $B0/${V0}1/.shard/$gfid_dst.2
TEST ! stat $M0/dir/src
TEST   stat $M0/dir/dst
TEST ! stat $B0/${V0}0/dir/src
TEST ! stat $B0/${V0}1/dir/src
TEST   stat $B0/${V0}0/dir/dst
TEST   stat $B0/${V0}1/dir/dst

###################################################
##### Rename of dst file with /.shard present #####
###################################################
TEST unlink $M0/dir/dst
TEST touch $M0/dir/src
TEST truncate -s 5M $M0/dir/dst
# Test to ensure that unlink of a sparse file works fine.
TEST mv -f $M0/dir/src $M0/dir/dst
TEST ! stat $M0/dir/src
TEST   stat $M0/dir/dst
TEST ! stat $B0/${V0}0/dir/src
TEST ! stat $B0/${V0}1/dir/src
TEST   stat $B0/${V0}0/dir/dst
TEST   stat $B0/${V0}1/dir/dst

###############################################################
##### Rename of dst file with only one block (the zeroth) #####
###############################################################
TEST unlink $M0/dir/dst
TEST touch $M0/dir/src
TEST dd if=/dev/zero of=$M0/dir/dst bs=1024 count=1024
# Test to ensure that unlink of a sparse file works fine.
TEST mv -f $M0/dir/src $M0/dir/dst
TEST ! stat $M0/dir/src
TEST   stat $M0/dir/dst
TEST ! stat $B0/${V0}0/dir/src
TEST ! stat $B0/${V0}1/dir/src
TEST   stat $B0/${V0}0/dir/dst
TEST   stat $B0/${V0}1/dir/dst

########################################################
##### Rename to a dst sharded file with hard-links #####
########################################################
TEST unlink $M0/dir/dst
TEST touch $M0/dir/src
# Create a 9M sharded file
TEST dd if=/dev/zero of=$M0/dir/dst bs=1024 count=9216
gfid_dst=$(get_gfid_string $M0/dir/dst)
# Ensure its shards are created.
TEST stat $B0/${V0}0/.shard/$gfid_dst.1
TEST stat $B0/${V0}1/.shard/$gfid_dst.1
TEST stat $B0/${V0}0/.shard/$gfid_dst.2
TEST stat $B0/${V0}1/.shard/$gfid_dst.2
# Create a hard link.
TEST ln $M0/dir/dst $M0/link
# Now rename src to the dst.
TEST mv -f $M0/dir/src $M0/dir/dst
# Ensure the shards are still intact.
TEST stat $B0/${V0}0/.shard/$gfid_dst.1
TEST stat $B0/${V0}1/.shard/$gfid_dst.1
TEST stat $B0/${V0}0/.shard/$gfid_dst.2
TEST stat $B0/${V0}1/.shard/$gfid_dst.2
TEST ! stat $M0/dir/src
TEST ! stat $B0/${V0}0/dir/src
TEST ! stat $B0/${V0}1/dir/src
# Now rename another file to the last link.
TEST touch $M0/dir/src2
TEST mv -f $M0/dir/src2 $M0/link
# Ensure that the shards are all cleaned up.
TEST ! stat $B0/${V0}0/.shard/$gfid_dst.1
TEST ! stat $B0/${V0}1/.shard/$gfid_dst.1
TEST ! stat $B0/${V0}0/.shard/$gfid_dst.2
TEST ! stat $B0/${V0}1/.shard/$gfid_dst.2
TEST ! stat $M0/dir/src2
TEST ! stat $B0/${V0}0/dir/src2
TEST ! stat $B0/${V0}1/dir/src2

# Rename with non-existent dst and a sharded src
TEST touch $M0/dir/src
TEST dd if=/dev/zero of=$M0/dir/src bs=1024 count=9216
gfid_src=$(get_gfid_string $M0/dir/src)
# Ensure its shards are created.
TEST stat $B0/${V0}0/.shard/$gfid_src.1
TEST stat $B0/${V0}1/.shard/$gfid_src.1
TEST stat $B0/${V0}0/.shard/$gfid_src.2
TEST stat $B0/${V0}1/.shard/$gfid_src.2
# Now rename src to the dst.
TEST mv $M0/dir/src $M0/dir/dst

TEST   stat $B0/${V0}0/.shard/$gfid_src.1
TEST   stat $B0/${V0}1/.shard/$gfid_src.1
TEST   stat $B0/${V0}0/.shard/$gfid_src.2
TEST   stat $B0/${V0}1/.shard/$gfid_src.2
TEST ! stat $M0/dir/src
TEST ! stat $B0/${V0}0/dir/src
TEST ! stat $B0/${V0}1/dir/src
TEST   stat $M0/dir/dst
TEST   stat $B0/${V0}0/dir/dst
TEST   stat $B0/${V0}1/dir/dst

# Rename with non-existent dst and a sharded src with no shards
TEST touch $M0/dir/src
TEST dd if=/dev/zero of=$M0/dir/src bs=1024 count=1024
gfid_src=$(get_gfid_string $M0/dir/src)
TEST ! stat $B0/${V0}0/.shard/$gfid_src.1
TEST ! stat $B0/${V0}1/.shard/$gfid_src.1
# Now rename src to the dst.
TEST mv $M0/dir/src $M0/dir/dst
TEST ! stat $M0/dir/src
TEST ! stat $B0/${V0}0/dir/src
TEST ! stat $B0/${V0}1/dir/src
TEST   stat $M0/dir/dst
TEST   stat $B0/${V0}0/dir/dst
TEST   stat $B0/${V0}1/dir/dst

cleanup
