#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

#Test cases to perform gfid-self-heal
#file 'a' should be assigned a fresh gfid
#file 'b' should be healed with gfid1 from brick1
#file 'c' should be healed with gfid2 from brick2

gfid1="0x8428b7193a764bf8be8046fb860b8993"
gfid2="0x85ad91afa2f74694bf52c3326d048209"

cleanup;
TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}0 $H0:$B0/${V0}1
TEST $CLI volume start $V0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0 --direct-io-mode=enable
touch $B0/${V0}0/a $B0/${V0}1/a
touch $B0/${V0}0/b $B0/${V0}1/b
touch $B0/${V0}0/c $B0/${V0}1/c

TEST setfattr -n trusted.gfid -v $gfid1 $B0/${V0}0/b
TEST setfattr -n trusted.gfid -v $gfid2 $B0/${V0}1/c

sleep 2

TEST stat $M0/a
TEST stat $M0/b
TEST stat $M0/c

TEST gf_get_gfid_xattr $B0/${V0}0/a
TEST gf_get_gfid_xattr $B0/${V0}1/a

EXPECT "$gfid1" gf_get_gfid_xattr $B0/${V0}0/b
EXPECT "$gfid1" gf_get_gfid_xattr $B0/${V0}1/b

EXPECT "$gfid2" gf_get_gfid_xattr $B0/${V0}0/c
EXPECT "$gfid2" gf_get_gfid_xattr $B0/${V0}1/c

cleanup;
