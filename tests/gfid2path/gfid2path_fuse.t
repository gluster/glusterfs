#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc

CLI_SETGFID2PATH="gluster-setgfid2path";

cleanup;

XXHSUM_SOURCE="$(dirname $0)/../../contrib/xxhash/xxhsum.c $(dirname $0)/../../contrib/xxhash/xxhash.c"
XXHSUM_EXEC=$(dirname $0)/xxhsum

## Build xxhsum C source
build_tester $XXHSUM_SOURCE -o $XXHSUM_EXEC -I$(dirname $0)/../../contrib/xxhash
TEST [ -e $XXHSUM_EXEC ]

TEST glusterd
TEST pidof glusterd

## Create a single brick volume (B=1)
TEST $CLI volume create $V0 $H0:$B0/${V0}1;
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';
EXPECT '1' brick_count $V0

## Start the volume
TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

## Mount the volume
TEST $GFS --volfile-server=$H0 --volfile-id=$V0 $M0;

## disable gfid2path
TEST $CLI volume set $V0 gfid2path disable

pgfid="00000000-0000-0000-0000-000000000001"
xxh64_file=$B0/${V0}1/xxh64_file

# Create a file before enabling gfid2path
fname=$M0/before_file1
touch $fname;
backpath=$B0/${V0}1/before_file1

# Set gfid2path xattr
TEST $CLI_SETGFID2PATH $backpath

#Check for the presence of xattr
pgfid_bname=$pgfid/before_file1
echo -n $pgfid_bname > $xxh64_file
xxh64sum=$($XXHSUM_EXEC $xxh64_file | awk '{print $1}')
key="trusted.gfid2path.$xxh64sum"
EXPECT $pgfid_bname get_text_xattr $key $backpath

## enable gfid2path
TEST $CLI volume set $V0 gfid2path enable

#CREATE
fname=$M0/file1
touch $fname;
backpath=$B0/${V0}1/file1

#Check for the presence of xattr
pgfid_bname=$pgfid/file1
echo -n $pgfid_bname > $xxh64_file
xxh64sum=$($XXHSUM_EXEC $xxh64_file | awk '{print $1}')
key="trusted.gfid2path.$xxh64sum"
EXPECT $pgfid_bname get_text_xattr $key $backpath

#MKNOD
fname=$M0/mknod_file1
mknod $fname p;
backpath=$B0/${V0}1/mknod_file1

#Check for the presence of xattr
pgfid_bname=$pgfid/mknod_file1
echo -n $pgfid_bname > $xxh64_file
xxh64sum=$($XXHSUM_EXEC $xxh64_file | awk '{print $1}')
key="trusted.gfid2path.$xxh64sum"
EXPECT $pgfid_bname get_text_xattr $key $backpath

#LINK
fname1=$M0/file1
fname2=$M0/hl_file1
ln $fname1 $fname2
backpath1=$B0/${V0}1/file1
backpath2=$B0/${V0}1/hl_file1

#Check for the presence of two xattrs
pgfid_bname=$pgfid/file1
echo -n $pgfid_bname > $xxh64_file
xxh64sum=$($XXHSUM_EXEC $xxh64_file | awk '{print $1}')
key="trusted.gfid2path.$xxh64sum"
EXPECT $pgfid_bname get_text_xattr $key $backpath1

pgfid_bname=$pgfid/hl_file1
echo -n $pgfid_bname > $xxh64_file
xxh64sum=$($XXHSUM_EXEC $xxh64_file | awk '{print $1}')
key="trusted.gfid2path.$xxh64sum"
EXPECT $pgfid_bname get_text_xattr $key $backpath2

#RENAME
fname1=$M0/file1
fname2=$M0/rn_file1
mv $fname1 $fname2
backpath=$B0/${V0}1/rn_file1

#Check for the presence of new xattr
pgfid_bname=$pgfid/file1
echo -n $pgfid_bname > $xxh64_file
xxh64sum=$($XXHSUM_EXEC $xxh64_file | awk '{print $1}')
key="trusted.gfid2path.$xxh64sum"
EXPECT_NOT $pgfid_bname get_text_xattr $key $backpath

pgfid_bname=$pgfid/rn_file1
echo -n $pgfid_bname > $xxh64_file
xxh64sum=$($XXHSUM_EXEC $xxh64_file | awk '{print $1}')
key="trusted.gfid2path.$xxh64sum"
EXPECT $pgfid_bname get_text_xattr $key $backpath

#UNLINK
fname1=$M0/hl_file1
rm -f $fname1
fname2=$M0/rn_file1
backpath=$B0/${V0}1/rn_file1

#Check removal of xattr
pgfid_bname=$pgfid/hl_file1
echo -n $pgfid_bname > $xxh64_file
xxh64sum=$($XXHSUM_EXEC $xxh64_file | awk '{print $1}')
key="trusted.gfid2path.$xxh64sum"
EXPECT_NOT $pgfid_bname get_text_xattr $key $backpath

pgfid_bname=$pgfid/rn_file1
echo -n $pgfid_bname > $xxh64_file
xxh64sum=$($XXHSUM_EXEC $xxh64_file | awk '{print $1}')
key="trusted.gfid2path.$xxh64sum"
EXPECT $pgfid_bname get_text_xattr $key $backpath

#SYMLINK
fname=rn_file1
sym_fname=$M0/sym_file1
ln -s $fname $sym_fname
backpath=$B0/${V0}1/sym_file1

#Check for the presence of xattr
pgfid_bname=$pgfid/sym_file1
echo -n $pgfid_bname > $xxh64_file
xxh64sum=$($XXHSUM_EXEC $xxh64_file | awk '{print $1}')
key="trusted.gfid2path.$xxh64sum"
EXPECT $pgfid_bname get_text_xattr $key $backpath

#FINAL UNLINK
fname=$M0/rn_file1
sym_fname=$M0/sym_file1
mknod_fname=$M0/mknod_file1

rm -f $fname
rm -f $sym_fname
rm -f $mknod_fname
TEST ! stat $fname
TEST ! stat $sym_fname
TEST ! stat $mknod_fname

#Cleanups
rm -f $STUB_EXEC
cleanup;
