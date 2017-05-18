#!/bin/bash

. $(dirname $0)/include.rc
. $(dirname $0)/volume.rc

write_data () {
        path=$1
        shift
        echo "$@" > $path
}

create_index_entry () {
        local brick=$1
        local gfid_str=$(gf_get_gfid_xattr $brick/$2)
        local gfid_path=$(gf_gfid_xattr_to_str $gfid_str)
        local xop_file=$(ls $brick/.glusterfs/indices/xattrop/xattrop-* \
                | tail -n1)
        ln $xop_file $brick/.glusterfs/indices/xattrop/$gfid_path
        setfattr -n trusted.glusterfs.validate-status -v suspect $brick/$2
}

get_vstatus () {
        getfattr --name trusted.glusterfs.validate-status --only-values $1 \
                2> /dev/null
}

trap cleanup EXIT

TEST glusterd
TEST $CLI volume create $V0 replica 3 $H0:$B0/${V0}{0,1,2}

# Comment out the following line to see the "argh" and "blah" tests at the end
# fail.  That's because normal self-heal can't deal with this particular
# condition.  To do that, we must check the actual data (OK, checksums).  That's
# expensive, but if there's corruption below us - e.g. filesystem bug, flaky
# disk - then it's what we have to do.
TEST $CLI volume set $V0 cluster.shd-validate-data on

TEST $CLI volume start $V0
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "Y" glustershd_up_status
TEST $GFS -s $H0 --volfile-id=$V0 $M0
TEST mkdir $M0/xyz
TEST write_data $M0/file-ok hello
TEST write_data $M0/file-use0 hello
TEST write_data $M0/xyz/file-use1 hello
TEST write_data $M0/file-bad hello
TEST umount $M0

# Corrupt a bunch of data.
TEST write_data $B0/${V0}2/file-use0 'argh!'
TEST write_data $B0/${V0}0/xyz/file-use1 'blah!'
TEST write_data $B0/${V0}1/file-bad 'diffX'
TEST write_data $B0/${V0}2/file-bad 'diffY'

# Add the files to their indices.
TEST create_index_entry $B0/${V0}0 file-ok
TEST create_index_entry $B0/${V0}1 file-ok
TEST create_index_entry $B0/${V0}2 file-ok
TEST create_index_entry $B0/${V0}0 file-use0
TEST create_index_entry $B0/${V0}1 file-use0
TEST create_index_entry $B0/${V0}2 file-use0
TEST create_index_entry $B0/${V0}0 xyz/file-use1
TEST create_index_entry $B0/${V0}1 xyz/file-use1
TEST create_index_entry $B0/${V0}2 xyz/file-use1
TEST create_index_entry $B0/${V0}0 file-bad
TEST create_index_entry $B0/${V0}1 file-bad
TEST create_index_entry $B0/${V0}2 file-bad

# Time to see what we can do.
TEST $CLI volume heal $V0

# These files are not marked in the normal way as needing heal (that's kind of
# the whole problem) so heal counts aren't useful.  There are only a few tiny
# files, so just wait a few seconds for the heal to complete.
sleep 5

# Test the contents of the files.
EXPECT hello cat $B0/${V0}0/file-ok
EXPECT hello cat $B0/${V0}1/file-ok
EXPECT hello cat $B0/${V0}2/file-ok
EXPECT hello cat $B0/${V0}0/file-use0
EXPECT hello cat $B0/${V0}1/file-use0
EXPECT hello cat $B0/${V0}2/file-use0
EXPECT hello cat $B0/${V0}0/xyz/file-use1
EXPECT hello cat $B0/${V0}1/xyz/file-use1
EXPECT hello cat $B0/${V0}2/xyz/file-use1
# This was in three-way split brain, so the replicas should still diverge.
EXPECT hello cat $B0/${V0}0/file-bad
EXPECT diffX cat $B0/${V0}1/file-bad
EXPECT diffY cat $B0/${V0}2/file-bad

# Now test validation states.
EXPECT clean get_vstatus $B0/${V0}0/file-ok
EXPECT clean get_vstatus $B0/${V0}1/file-ok
EXPECT clean get_vstatus $B0/${V0}2/file-ok
EXPECT clean get_vstatus $B0/${V0}0/file-use0
EXPECT clean get_vstatus $B0/${V0}1/file-use0
EXPECT repaired get_vstatus $B0/${V0}2/file-use0
EXPECT repaired get_vstatus $B0/${V0}0/xyz/file-use1
EXPECT clean get_vstatus $B0/${V0}1/xyz/file-use1
EXPECT clean get_vstatus $B0/${V0}2/xyz/file-use1
EXPECT suspect get_vstatus $B0/${V0}0/file-bad
EXPECT suspect get_vstatus $B0/${V0}1/file-bad
EXPECT suspect get_vstatus $B0/${V0}2/file-bad

print_summary () {
        for f in file-ok file-bad file-use0 file-use1; do
                echo "=== FILE $f"
                find $B0/ -name $f | xargs grep -E .
                find $B0/ -name $f | xargs getfattr -d -e text \
                                     -m trusted.glusterfs.validate-status
        done
        echo "=== ORPHANS"
        find $B0 -name '*.orig' | xargs grep -E .
        find $B0 -name '*.link' | xargs ls -l
}

#print_summary
