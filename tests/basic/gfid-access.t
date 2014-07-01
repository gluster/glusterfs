#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}0
TEST $CLI volume start $V0
TEST glusterfs --volfile-id=/$V0 --volfile-server=$H0 $M0 --attribute-timeout=0 --entry-timeout=0 --aux-gfid-mount
TEST mkdir $M0/a
TEST touch $M0/b
a_gfid_str=$(gf_gfid_xattr_to_str $(gf_get_gfid_xattr $B0/${V0}0/a))
b_gfid_str=$(gf_gfid_xattr_to_str $(gf_get_gfid_xattr $B0/${V0}0/b))

#Operations on Directory
TEST setfattr -n trusted.abc -v abc $M0/a
EXPECT "abc" echo $(getfattr -n trusted.abc $M0/a)
EXPECT "abc" echo $(getfattr -n trusted.abc $M0/.gfid/$a_gfid_str)
TEST setfattr -x trusted.abc $M0/a
TEST ! getfattr -n trusted.abc $M0/a
TEST ! getfattr -n trusted.abc $M0/.gfid/$a_gfid_str
TEST chmod 0777 $M0/a
EXPECT "777" stat -c "%a" $M0/a
EXPECT "777" stat -c "%a" $M0/.gfid/$a_gfid_str

TEST setfattr -n trusted.abc -v def $M0/.gfid/$a_gfid_str
EXPECT "def" echo $(getfattr -n trusted.abc $M0/a)
EXPECT "def" echo $(getfattr -n trusted.abc $M0/.gfid/$a_gfid_str)
TEST setfattr -x trusted.abc $M0/.gfid/$a_gfid_str
TEST ! getfattr -n trusted.abc $M0/a
TEST ! getfattr -n trusted.abc $M0/.gfid/$a_gfid_str
TEST chmod 0777 $M0/.gfid/$a_gfid_str
EXPECT "777" stat -c "%a" $M0/a
EXPECT "777" stat -c "%a" $M0/.gfid/$a_gfid_str

#Operations on File
TEST setfattr -n trusted.abc -v abc $M0/b
EXPECT "abc" echo $(getfattr -n trusted.abc $M0/b)
EXPECT "abc" echo $(getfattr -n trusted.abc $M0/.gfid/$b_gfid_str)
TEST setfattr -x trusted.abc $M0/b
TEST ! getfattr -n trusted.abc $M0/b
TEST ! getfattr -n trusted.abc $M0/.gfid/$b_gfid_str
TEST chmod 0777 $M0/b
EXPECT "777" stat -c "%a" $M0/b
EXPECT "777" stat -c "%a" $M0/.gfid/$b_gfid_str

TEST setfattr -n trusted.abc -v def $M0/.gfid/$b_gfid_str
EXPECT "def" echo $(getfattr -n trusted.abc $M0/b)
EXPECT "def" echo $(getfattr -n trusted.abc $M0/.gfid/$b_gfid_str)
TEST setfattr -x trusted.abc $M0/.gfid/$b_gfid_str
TEST ! getfattr -n trusted.abc $M0/b
TEST ! getfattr -n trusted.abc $M0/.gfid/$b_gfid_str
TEST chmod 0777 $M0/.gfid/$b_gfid_str
EXPECT "777" stat -c "%a" $M0/b
EXPECT "777" stat -c "%a" $M0/.gfid/$b_gfid_str

cleanup
