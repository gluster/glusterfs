#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}0
TEST $CLI volume start $V0
TEST $GFS --volfile-id=/$V0 --volfile-server=$H0 $M0 --aux-gfid-mount;
TEST mkdir $M0/a
TEST touch $M0/b
a_gfid_str=$(gf_gfid_xattr_to_str $(gf_get_gfid_xattr $B0/${V0}0/a))
b_gfid_str=$(gf_gfid_xattr_to_str $(gf_get_gfid_xattr $B0/${V0}0/b))

#Operations on Directory
TEST setfattr -n trusted.abc -v abc $M0/a
EXPECT "abc" echo $(getfattr -n trusted.abc --only-values $M0/a)
EXPECT "abc" echo $(getfattr -n trusted.abc --only-values $M0/.gfid/$a_gfid_str)
TEST setfattr -x trusted.abc $M0/a
TEST ! getfattr -n trusted.abc $M0/a
TEST ! getfattr -n trusted.abc $M0/.gfid/$a_gfid_str
TEST chmod 0777 $M0/a
EXPECT "777" stat -c "%a" $M0/a
EXPECT "777" stat -c "%a" $M0/.gfid/$a_gfid_str

TEST setfattr -n trusted.abc -v def $M0/.gfid/$a_gfid_str
EXPECT "def" echo $(getfattr -n trusted.abc --only-values $M0/a)
EXPECT "def" echo $(getfattr -n trusted.abc --only-values $M0/.gfid/$a_gfid_str)
TEST setfattr -x trusted.abc $M0/.gfid/$a_gfid_str
TEST ! getfattr -n trusted.abc $M0/a
TEST ! getfattr -n trusted.abc $M0/.gfid/$a_gfid_str
TEST chmod 0777 $M0/.gfid/$a_gfid_str
EXPECT "777" stat -c "%a" $M0/a
EXPECT "777" stat -c "%a" $M0/.gfid/$a_gfid_str

#Entry operations on directory
#Test that virtual directories are not allowed to be deleted.
TEST ! mkdir $M0/.gfid
TEST ! rmdir $M0/.gfid
TEST ! touch $M0/.gfid
TEST ! rm -f $M0/.gfid
TEST ! mv $M0/.gfid $M0/dont-rename
TEST ! ln -s $M0/symlink $M0/.gfid
TEST ! ln $M0/.gfid $M0/hlink
TEST ! mknod $M0/.gfid b 0 0

#Test that first level directory/file creations inside .gfid are not allowed.
tmpfile=$(mktemp)
TEST ! mkdir $M0/.gfid/a
TEST ! touch $M0/.gfid/a
TEST ! mv $tmpfile $M0/.gfid
TEST ! mv $M0/a $M0/.gfid
TEST ! mknod $M0/.gfid/b b 0 0
rm -f $tmpfile

#Operations on File
TEST setfattr -n trusted.abc -v abc $M0/b
EXPECT "abc" echo $(getfattr -n trusted.abc --only-values $M0/b)
EXPECT "abc" echo $(getfattr -n trusted.abc --only-values $M0/.gfid/$b_gfid_str)
TEST setfattr -x trusted.abc $M0/b
TEST ! getfattr -n trusted.abc $M0/b
TEST ! getfattr -n trusted.abc $M0/.gfid/$b_gfid_str
TEST chmod 0777 $M0/b
EXPECT "777" stat -c "%a" $M0/b
EXPECT "777" stat -c "%a" $M0/.gfid/$b_gfid_str

TEST setfattr -n trusted.abc -v def $M0/.gfid/$b_gfid_str
EXPECT "def" echo $(getfattr -n trusted.abc --only-values $M0/b)
EXPECT "def" echo $(getfattr -n trusted.abc --only-values $M0/.gfid/$b_gfid_str)
TEST setfattr -x trusted.abc $M0/.gfid/$b_gfid_str
TEST ! getfattr -n trusted.abc $M0/b
TEST ! getfattr -n trusted.abc $M0/.gfid/$b_gfid_str
TEST chmod 0777 $M0/.gfid/$b_gfid_str
EXPECT "777" stat -c "%a" $M0/b
EXPECT "777" stat -c "%a" $M0/.gfid/$b_gfid_str

cleanup
