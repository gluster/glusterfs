#!/bin/bash
logdir=$(gluster --print-logdir)
BRICK_LOGFILES="$logdir/bricks/d-backends-brick?.log"
rm -f $BRICK_LOGFILES &> /dev/null

# Test that lock revocation works

. $(dirname $0)/../include.rc
. $(dirname $0)/../volume.rc
cleanup;

function deadlock_fop() {
  local MNT=$1
  for i in {1..1000}; do
    dd if=/dev/zero of=$MNT/testfile bs=1k count=10 &> /dev/null
    if grep "MONKEY LOCKING" $BRICK_LOGFILES &> /dev/null; then
      break
    fi
  done
}

function monkey_unlock() {
  grep "MONKEY LOCKING" $BRICK_LOGFILES &> /dev/null && echo SUCCESS
  return 0
}

function append_to_file() {
  local FILE_PATH=$1
  echo "hello" >> $FILE_PATH
  return 0
}

#Init
TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 replica 2 $H0:$B0/brick{0,1}
TEST $CLI volume set $V0 self-heal-daemon off
TEST $CLI volume set $V0 features.locks-monkey-unlocking on
TEST $CLI volume set $V0 features.locks-revocation-secs 2
TEST $CLI volume start $V0
TEST $GFS --volfile-id=$V0 -s $H0 $M0;
TEST $GFS --volfile-id=$V0 -s $H0 $M1;

# Deadlock writes to a file using monkey unlocking
deadlock_fop $M0 &
EXPECT_WITHIN 60 "SUCCESS" monkey_unlock

# Sleep > unlock timeout and attempt to write to the file
sleep 3
TEST append_to_file $M1/testfile

cleanup
#G_TESTDEF_TEST_STATUS_NETBSD7=BAD_TEST,BUG=1369401
