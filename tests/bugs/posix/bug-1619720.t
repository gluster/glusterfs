#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../dht.rc

cleanup;


# Test steps:
# The test checks to make sure that the trusted.pgfid.xx xattr is set on
# both the linkto and data files post the final rename.
# The test creates files file-1 and file-3 so that src_hashed = dst_hashed,
# src_cached = dst_cached and xxx_hashed != xxx_cached.
# It then renames file-1 to file-3 which triggers the posix_mknod call
# which updates the trusted.pgfid.xx xattr.


TEST glusterd
TEST pidof glusterd

TEST $CLI volume create $V0 $H0:$B0/${V0}0 $H0:$B0/${V0}1
TEST $CLI volume start $V0
TEST $CLI volume set $V0 storage.build-pgfid on

## Mount FUSE
TEST glusterfs -s $H0 --volfile-id $V0 $M0;

TEST mkdir $M0/tmp



# Not the best way to do this but I need files which hash to the same subvol and
# whose cached subvols are the same.
# In a 2 subvol distributed volume, file-{1,3} hash to the same subvol.
# file-2 will hash to the other subvol

TEST touch $M0/tmp/file-2
pgfid_xattr_name=$(getfattr -m "trusted.pgfid.*" $B0/${V0}1/tmp/file-2 | grep "trusted.pgfid")
echo $pgfid_xattr_name


TEST mv $M0/tmp/file-2 $M0/tmp/file-1
TEST touch $M0/tmp/file-2
TEST mv $M0/tmp/file-2 $M0/tmp/file-3

# At this point, both the file-1 and file-3 data files exist on one subvol
# and both linkto files on the other

TEST mv -f $M0/tmp/file-1 $M0/tmp/file-3

TEST getfattr -n $pgfid_xattr_name $B0/${V0}0/tmp/file-3
TEST getfattr -n $pgfid_xattr_name $B0/${V0}1/tmp/file-3

# Not required for the test but an extra check if required.
# The linkto file was not renamed Without the fix.
#TEST mv $M0/tmp/file-3 $M0/tmp/file-6
#cleanup;
