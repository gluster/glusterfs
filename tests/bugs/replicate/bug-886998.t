#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

# This tests that the replicate trash directory(.landfill) has following
# properties.
# Note: This is to have backward compatibility with 3.3 glusterfs
#       In the latest releases this dir is present inside .glusterfs of brick.
# 1) lookup of trash dir fails
# 2) readdir does not show this directory
# 3) Self-heal does not do any self-heal of these directories.
gfid1="0xc2e75dde97f346e7842d1076a8e699f8"
TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/${V0}0 $H0:$B0/${V0}1
TEST $CLI volume start $V0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0 --direct-io-mode=enable

TEST mkdir $B0/${V0}1/.landfill
TEST setfattr -n trusted.gfid -v $gfid1 $B0/${V0}1/.landfill
TEST mkdir $B0/${V0}0/.landfill
TEST setfattr -n trusted.gfid -v $gfid1 $B0/${V0}0/.landfill

TEST ! stat $M0/.landfill
EXPECT "" echo $(ls -a $M0 | grep ".landfill")

TEST rmdir $B0/${V0}0/.landfill
#Force a conservative merge and it should not create .landfill
TEST setfattr -n trusted.afr.$V0-client-0 -v 0x000000000000000000000000 $B0/${V0}0/
TEST setfattr -n trusted.afr.$V0-client-1 -v 0x000000000000000000000001 $B0/${V0}0/

TEST setfattr -n trusted.afr.$V0-client-0 -v 0x000000000000000000000001 $B0/${V0}1/
TEST setfattr -n trusted.afr.$V0-client-1 -v 0x000000000000000000000000 $B0/${V0}1/

EXPECT "" echo $(ls -a $M0 | grep ".landfill")
TEST ! stat $B0/${V0}0/.landfill
TEST stat $B0/${V0}1/.landfill

#TEST that the dir is not deleted even when xattrs suggest to delete
TEST setfattr -n trusted.afr.$V0-client-0 -v 0x000000000000000000000000 $B0/${V0}0/
TEST setfattr -n trusted.afr.$V0-client-1 -v 0x000000000000000000000001 $B0/${V0}0/

TEST setfattr -n trusted.afr.$V0-client-0 -v 0x000000000000000000000000 $B0/${V0}1/
TEST setfattr -n trusted.afr.$V0-client-1 -v 0x000000000000000000000000 $B0/${V0}1/

EXPECT "" echo $(ls -a $M0 | grep ".landfill")
TEST ! stat $B0/${V0}0/.landfill
TEST stat $B0/${V0}1/.landfill
cleanup;
