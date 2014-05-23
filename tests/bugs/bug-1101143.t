#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

#This file checks if softlinks are removed or not when rename is done while
#a brick is down in a replica pair.
cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}{0,1}
TEST $CLI volume set $V0 brick-log-level DEBUG
TEST $CLI volume start $V0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0
cd $M0
TEST mkdir -p d1/d2/d3
TEST mkdir -p dir1/dir1
d1_gfid_path=$(gf_get_gfid_backend_file_path $B0/${V0}0 d1)
d1_gfid=$(gf_get_gfid_xattr $B0/${V0}0/d1)
dir1_gfid_path=$(gf_get_gfid_backend_file_path $B0/${V0}0 dir1)
dir1_dir1_gfid_path=$(gf_get_gfid_backend_file_path $B0/${V0}0 dir1/dir1)
d1_d2_d3_old_gfid_path=$(gf_get_gfid_backend_file_path $B0/${V0}0 d1/d2/d3)
d1_d2_d3_gfid=$(gf_get_gfid_xattr $B0/${V0}0/d1/d2/d3)
TEST kill_brick $V0 $H0 $B0/${V0}0

#Rename should not delete gfid-link by janitor
TEST mv d1 d2

#Rmdir should delete gfid-link by janitor
TEST rm -rf dir1

#Stale-link will be created if we do this after self-heal where old-gfid dir will be pointing to dir with different gfid on brick-0
TEST rmdir d2/d2/d3
TEST mkdir d2/d2/d3

TEST $CLI volume set $V0 self-heal-daemon off
TEST $CLI volume start $V0 force
#Wait for the brick to come up
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status $V0 0

#Now enable self-heal-daemon so that heal will populate landfill
TEST $CLI volume set $V0 self-heal-daemon on
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 0
EXPECT_WITHIN $CHILD_UP_TIMEOUT "1" afr_child_up_status_in_shd $V0 1
TEST $CLI volume heal $V0 full
EXPECT_WITHIN $HEAL_TIMEOUT "0" afr_get_pending_heal_count $V0
EXPECT_NOT "0" landfill_entry_count $B0/${V0}0

#Janitor thread walks once every 10 minutes, or at the time of brick start
#So lets stop the volume and make some checks.
TEST $CLI volume stop $V0

#Test that it is pointing to gfid which is not the old one
TEST stat $d1_d2_d3_old_gfid_path #Check there is stale link file
new_gfid=$(getfattr -d -m. -e hex $d1_d2_d3_old_gfid_path | grep gfid| cut -f2 -d'=')
#Check the gfids are valid
EXPECT 34 expr length $new_gfid
EXPECT 34 expr length $d1_d2_d3_gfid
EXPECT_NOT $new_gfid echo $d1_d2_d3_gfid

#restart it to make sure janitor wakes up.
TEST $CLI volume start $V0 force
EXPECT_WITHIN $JANITOR_TIMEOUT "0" landfill_entry_count $B0/${V0}0

#After janitor cleans up, check that the directories have their softlinks
d2_gfid_path=$(gf_get_gfid_backend_file_path $B0/${V0}0 d2)
d2_gfid=$(gf_get_gfid_xattr $B0/${V0}0/d2)
EXPECT "$d1_gfid_path" echo $d2_gfid_path
EXPECT "$d1_gfid" echo $d2_gfid
TEST stat $d1_gfid_path

#TODO afr-v2 is not healing sub-directories because heal is not marking
#new-entry changelog?. Will need to fix that. After the fix, the following line
#needs to be un-commented.
#TEST stat $(gf_get_gfid_backend_file_path $B0/${V0}0 d2/d2)

#Check that janitor removed stale links
TEST ! stat $dir1_gfid_path
TEST ! stat $dir1_dir1_gfid_path
TEST ! stat $d1_d2_d3_old_gfid_path #Check stale link file is deleted
cleanup
