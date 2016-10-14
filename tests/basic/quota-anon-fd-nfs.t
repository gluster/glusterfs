#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
. $(dirname $0)/../nfs.rc
. $(dirname $0)/../fileio.rc

cleanup;

QDD=$(dirname $0)/quota
# compile the test write program and run it
build_tester $(dirname $0)/quota.c -o $QDD

TESTS_EXPECTED_IN_LOOP=16
TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;

TEST $CLI volume create $V0 $H0:$B0/brick1;
EXPECT 'Created' volinfo_field $V0 'Status';
TEST $CLI volume set $V0 nfs.disable false


# The test makes use of inode-lru-limit to hit a scenario, where we
# find an inode whose ancestry is not there. Following is the
# hypothesis (which is confirmed by seeing logs indicating that
# codepath has been executed, but not through a good understanding of
# NFS internals).

#     At the end of an fop, the reference count of an inode would be
#     zero. The inode (and its ancestry) persists in memory only
#     because of non-zero lookup count. These looked up inodes are put
#     in an lru queue of size 1 (here). So, there can be at most one
#     such inode in memory.

#     NFS Server makes use of anonymous fds. So, if it cannot find
#     valid fd, it does a nameless lookup. This gives us an inode
#     whose ancestry is NULL. When a write happens on this inode,
#     quota-enforcer/marker finds a NULL ancestry and asks
#     storage/posix to build it.

TEST $CLI volume set $V0 network.inode-lru-limit 1
TEST $CLI volume set $V0 performance.nfs.write-behind off

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status';

TEST $CLI volume quota $V0 enable
TEST $CLI volume quota $V0 limit-usage / 1
TEST $CLI volume quota $V0 soft-timeout 0
TEST $CLI volume quota $V0 hard-timeout 0

TEST mount_nfs $H0:/$V0 $N0 noac,soft,nolock,vers=3;
deep=/0/1/2/3/4/5/6/7/8/9
TEST mkdir -p $N0/$deep

TEST touch $N0/$deep/file1 $N0/$deep/file2 $N0/$deep/file3 $N0/$deep/file4

TEST fd_open 3 'w' "$N0/$deep/file1"
TEST fd_open 4 'w' "$N0/$deep/file2"
TEST fd_open 5 'w' "$N0/$deep/file3"
TEST fd_open 6 'w' "$N0/$deep/file4"

# consume all quota
echo "Hello" > $N0/$deep/new_file_1
echo "World" >> $N0/$deep/new_file_1
echo 1 >> $N0/$deep/new_file_1
echo 2 >> $N0/$deep/new_file_1

# Try to create a 1M file which should fail
TEST ! $QDD $N0/$deep/new_file_2 256 4


# At the end of each fop in server, reference count of the
# inode associated with each of the file above drops to zero and hence
# put into lru queue. Since lru-limit is set to 1, an fop next file
# will displace the current inode from itable. This will ensure that
# when writes happens on same fd, fd resolution results in
# nameless lookup from server and quota_writev encounters an fd
# associated with an inode whose parent is not present in itable.

for j in $(seq 1 2); do
    for i in $(seq 3 6); do
        # failing writes indicate that we are enforcing quota set on /
        # even with anonymous fds.
        TEST_IN_LOOP ! fd_write $i "content"
        TEST_IN_LOOP sync
    done
done

exec 3>&-
exec 4>&-
exec 5>&-
exec 6>&-

$CLI volume statedump $V0 all

EXPECT_WITHIN $UMOUNT_TIMEOUT "Y" force_umount $N0

# This is ugly, but there seems to be a latent race between other actions and
# stopping the volume.  The visible symptom is that "umount -l" (run from
# gf_umount_lazy in glusterd) hangs.  This happens pretty consistently with the
# new mem-pool code, though it's not really anything to do with memory pools -
# just with changed timing.  Adding the sleep here makes it work consistently.
#
# If anyone else wants to debug the race condition, feel free.
sleep 3

TEST $CLI volume stop $V0
EXPECT "1" get_aux

rm -f $QDD

cleanup;
#G_TESTDEF_TEST_STATUS_NETBSD7=BAD_TEST,BUG=000000
