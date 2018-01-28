#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../afr.rc

cleanup;

## Start and create a volume
TEST glusterd;
TEST pidof glusterd;
TEST $CLI volume info;

TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2};
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';
TEST $CLI volume set $V0 self-heal-daemon off
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0

###############################################################################yy
# Case of 2 bricks blaming the third and the third blaming the other two.

TEST `echo "hello" >> $M0/file`

# B0 and B2 must blame B1
TEST kill_brick $V0 $H0 $B0/$V0"1"
TEST `echo "append" >> $M0/file`
EXPECT "00000001" afr_get_specific_changelog_xattr $B0/${V0}0/file trusted.afr.$V0-client-1 data
EXPECT "00000001" afr_get_specific_changelog_xattr $B0/${V0}2/file trusted.afr.$V0-client-1 data
CLIENT_MD5=$(md5sum $M0/file | cut -d\  -f1)

# B1 must blame B0 and B2
setfattr -n trusted.afr.$V0-client-0 -v 0x000000010000000000000000 $B0/$V0"1"/file
setfattr -n trusted.afr.$V0-client-2 -v 0x000000010000000000000000 $B0/$V0"1"/file

# Launch heal
TEST $CLI volume start $V0 force
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "1" brick_up_status $V0 $H0 $B0/${V0}1
TEST $CLI volume set $V0 self-heal-daemon on
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 2
TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0
B0_MD5=$(md5sum $B0/${V0}0/file | cut -d\  -f1)
B1_MD5=$(md5sum $B0/${V0}1/file | cut -d\  -f1)
B2_MD5=$(md5sum $B0/${V0}2/file | cut -d\  -f1)
TEST [ "$CLIENT_MD5" == "$B0_MD5" ]
TEST [ "$CLIENT_MD5" == "$B1_MD5" ]
TEST [ "$CLIENT_MD5" == "$B2_MD5" ]

TEST rm $M0/file

###############################################################################yy
# Case of each brick blaming the next one in a cyclic manner

TEST `echo "hello" >> $M0/file`
# Mark cyclic xattrs and modify file content directly on the bricks.
TEST $CLI volume set $V0 self-heal-daemon off
setfattr -n trusted.afr.$V0-client-1 -v 0x000000010000000000000000 $B0/$V0"0"/file
setfattr -n trusted.afr.dirty -v 0x000000010000000000000000 $B0/$V0"0"/file
setfattr -n trusted.afr.$V0-client-2 -v 0x000000010000000000000000 $B0/$V0"1"/file
setfattr -n trusted.afr.dirty -v 0x000000010000000000000000 $B0/$V0"1"/file
setfattr -n trusted.afr.$V0-client-0 -v 0x000000010000000000000000 $B0/$V0"2"/file
setfattr -n trusted.afr.dirty -v 0x000000010000000000000000 $B0/$V0"2"/file

TEST `echo "ab" >> $B0/$V0"0"/file`
TEST `echo "cdef" >> $B0/$V0"1"/file`
TEST `echo "ghi" >> $B0/$V0"2"/file`

# Add entry to xattrop dir to trigger index heal.
xattrop_dir0=$(afr_get_index_path $B0/$V0"0")
base_entry_b0=`ls $xattrop_dir0`
gfid_str=$(gf_gfid_xattr_to_str $(gf_get_gfid_xattr $B0/$V0"0"/file))
ln $xattrop_dir0/$base_entry_b0 $xattrop_dir0/$gfid_str
EXPECT_WITHIN $HEAL_TIMEOUT "^1$" get_pending_heal_count $V0

# Launch heal
TEST $CLI volume set $V0 self-heal-daemon on
TEST $CLI volume heal $V0
EXPECT_WITHIN $HEAL_TIMEOUT "^0$" get_pending_heal_count $V0
B0_MD5=$(md5sum $B0/${V0}0/file | cut -d\  -f1)
B1_MD5=$(md5sum $B0/${V0}1/file | cut -d\  -f1)
B2_MD5=$(md5sum $B0/${V0}2/file | cut -d\  -f1)
TEST [ "$B0_MD5" == "$B1_MD5" ]
TEST [ "$B0_MD5" == "$B2_MD5" ]
###############################################################################yy
cleanup
