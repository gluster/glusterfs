#!/bin/bash

# Issue #4849: link() fails with EEXIST when the same name was previously
# used as a hard link and then replaced via rename(). posix_rename() never
# removed the stale trusted.gfid2path.* entry for the overwritten name from
# the victim inode, so a later link() re-creating that name on the same
# inode failed at the XATTR_CREATE of the gfid2path xattr. This test works
# on the brick backend files, where the gfid2path entries live, so that it
# catches the stale entry itself and not just the failing link().

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}1;
EXPECT "$V0" volinfo_field $V0 'Volume Name';
EXPECT 'Created' volinfo_field $V0 'Status';

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

TEST $GFS --volfile-server=$H0 --volfile-id=$V0 $M0;

B=$B0/${V0}1
PGFID="00000000-0000-0000-0000-000000000001"

# xxhsum (from contrib) to compute gfid2path keys
if test -z $(type -p xxhsum); then
    XXHSUM_SOURCE="$(dirname $0)/../../../contrib/xxhash/xxhsum.c $(dirname $0)/../../../contrib/xxhash/xxhash.c"
    XXHSUM_EXEC=$B0/xxhsum
    build_tester $XXHSUM_SOURCE -o $XXHSUM_EXEC -I$(dirname $0)/../../../contrib/xxhash
else
    XXHSUM_EXEC=$(type -p xxhsum)
fi
TEST [ -x $XXHSUM_EXEC ]

# gfid_of <backend-path> : prints the trusted.gfid of the backend file/dir
# as a UUID string
function gfid_of {
    gf_gfid_xattr_to_str $(gf_get_gfid_xattr $1)
}

# gfid2path_key <pgfid> <bname> : prints trusted.gfid2path.<xxh64(pgfid/bname)>
function gfid2path_key {
    echo -n "$1/$2" > $B/xxh64_file
    echo "trusted.gfid2path.$($XXHSUM_EXEC $B/xxh64_file 2>/dev/null | awk '{print $1}')"
}

# gfid2path_root_key <bname> : same, for entries in the volume root,
# whose parent gfid is fixed
function gfid2path_root_key {
    gfid2path_key $PGFID $1
}

# gfid2path_count <backend-path> : number of gfid2path entries on the file
function gfid2path_count {
    getfattr -h -d -m 'trusted.gfid2path' $1 2>/dev/null | grep -c 'trusted.gfid2path'
}

##### Case 1: the reported repro #####

TEST touch $M0/file1
TEST touch $M0/file2
TEST ln $M0/file2 $M0/ln1
EXPECT "$PGFID/ln1" get_text_xattr $(gfid2path_root_key ln1) $B/file2

TEST mv $M0/file1 $M0/ln1
# the victim (file2) must not retain the stale entry for the overwritten name
EXPECT_NOT "$PGFID/ln1" get_text_xattr $(gfid2path_root_key ln1) $B/file2
# the source inode now owns the new name
EXPECT "$PGFID/ln1" get_text_xattr $(gfid2path_root_key ln1) $B/ln1

TEST mv $M0/ln1 $M0/ln2
EXPECT_NOT "$PGFID/ln1" get_text_xattr $(gfid2path_root_key ln1) $B/ln2
EXPECT "$PGFID/ln2" get_text_xattr $(gfid2path_root_key ln2) $B/ln2

# this link used to fail with EEXIST before the fix
TEST ln $M0/file2 $M0/ln1
EXPECT "$PGFID/ln1" get_text_xattr $(gfid2path_root_key ln1) $B/file2

##### Case 2: rename(2) between two hard links of one file #####
# POSIX mandates that renaming a name onto another hard link of the same
# file succeeds without performing any action (the kernel short-circuits
# it before any filesystem involvement). GNU mv refuses such renames in
# userspace, so call rename(2) from a helper program. Freeze the current
# behavior: success, and the gfid2path entries stay untouched. If such a
# rename were ever delivered to posix_rename (victim == source inode),
# the removal-before-set ordering in posix_rename() is what keeps the
# entries consistent.

RMSRC=$B0/rename_same.c
cat > $RMSRC <<EOF
#include <stdio.h>
int
main (int argc, char **argv)
{
    if (argc != 3)
        return 2;
    if (rename (argv[1], argv[2]) != 0) {
        perror ("rename");
        return 1;
    }
    return 0;
}
EOF
build_tester $RMSRC -o $B0/rename_same
rm -f $RMSRC

TEST touch $M0/f
TEST ln $M0/f $M0/a
TEST ln $M0/f $M0/b
TEST $B0/rename_same $M0/a $M0/b
EXPECT "$PGFID/a" get_text_xattr $(gfid2path_root_key a) $B/f
EXPECT "$PGFID/b" get_text_xattr $(gfid2path_root_key b) $B/f
# all three names still exist on the shared inode: f, a and b
EXPECT '3' gfid2path_count $B/f
TEST stat $M0/a
TEST stat $M0/b

##### Case 3: victim with nlink == 1 is fully replaced #####

TEST touch $M0/old
TEST touch $M0/new
TEST mv $M0/new $M0/old
EXPECT "$PGFID/old" get_text_xattr $(gfid2path_root_key old) $B/old
TEST ln $M0/old $M0/old_link
EXPECT "$PGFID/old_link" get_text_xattr $(gfid2path_root_key old_link) $B/old

##### Case 4: gfid2path disabled - operational regressions only #####

TEST $CLI volume set $V0 storage.gfid2path off
TEST touch $M0/o1
TEST touch $M0/o2
TEST ln $M0/o2 $M0/o_ln
TEST mv $M0/o1 $M0/o_ln
TEST mv $M0/o_ln $M0/o_ln2
TEST ln $M0/o2 $M0/o_ln
TEST $CLI volume set $V0 storage.gfid2path on

##### Case 5: overwrite inside a subdirectory (pgfid != root) #####

TEST mkdir $M0/d1
D1GFID=$(gfid_of $B/d1)
TEST [ -n "$D1GFID" ]

TEST touch $M0/file3
TEST touch $M0/file4
TEST ln $M0/file3 $M0/d1/x
EXPECT "$D1GFID/x" get_text_xattr $(gfid2path_key $D1GFID x) $B/file3

TEST mv $M0/file4 $M0/d1/x
EXPECT_NOT "$D1GFID/x" get_text_xattr $(gfid2path_key $D1GFID x) $B/file3
# file4's inode now lives under the d1/x dentry
EXPECT "$D1GFID/x" get_text_xattr $(gfid2path_key $D1GFID x) $B/d1/x

TEST mv $M0/d1/x $M0/d1/y
EXPECT_NOT "$D1GFID/x" get_text_xattr $(gfid2path_key $D1GFID x) $B/d1/y
EXPECT "$D1GFID/y" get_text_xattr $(gfid2path_key $D1GFID y) $B/d1/y

# the reported bug pattern inside a subdirectory
TEST ln $M0/file3 $M0/d1/x
EXPECT "$D1GFID/x" get_text_xattr $(gfid2path_key $D1GFID x) $B/file3

##### Case 6: symlink victim that keeps other names (nlink > 1) #####

TEST touch $M0/file5
TEST touch $M0/file6

TEST ln -s file5 $M0/sym1
EXPECT "$PGFID/sym1" get_text_xattr $(gfid2path_root_key sym1) $B/sym1

# link(2) does not dereference symlinks, so this hard-links the symlink
# itself and keeps its inode alive after sym1 is overwritten
TEST ln $M0/sym1 $M0/sym1b
EXPECT "$PGFID/sym1b" get_text_xattr $(gfid2path_root_key sym1b) $B/sym1b

TEST mv $M0/file6 $M0/sym1
# the symlink inode must not retain the entry of its overwritten name
EXPECT_NOT "$PGFID/sym1" get_text_xattr $(gfid2path_root_key sym1) $B/sym1b
EXPECT "$PGFID/sym1b" get_text_xattr $(gfid2path_root_key sym1b) $B/sym1b
# file6's inode now lives under the sym1 dentry
EXPECT "$PGFID/sym1" get_text_xattr $(gfid2path_root_key sym1) $B/sym1
TEST [ -L $M0/sym1b ]
TEST [ -f $M0/sym1 ]
TEST [ ! -L $M0/sym1 ]

##### Case 7: symlink freeze tests #####

# a single-named symlink loses its only name on overwrite; the stale
# entry dies with the inode, so only the visible outcome is frozen here
TEST touch $M0/file7
TEST ln -s file5 $M0/sym5
TEST mv $M0/file7 $M0/sym5
TEST [ -f $M0/sym5 ]
TEST [ ! -L $M0/sym5 ]

# renaming a symlink moves the symlink itself and updates its entries
TEST ln -s file5 $M0/sym3
EXPECT "$PGFID/sym3" get_text_xattr $(gfid2path_root_key sym3) $B/sym3
TEST mv $M0/sym3 $M0/sym4
EXPECT_NOT "$PGFID/sym3" get_text_xattr $(gfid2path_root_key sym3) $B/sym4
EXPECT "$PGFID/sym4" get_text_xattr $(gfid2path_root_key sym4) $B/sym4
TEST [ -L $M0/sym4 ]

rm -f $B/xxh64_file
cleanup;
