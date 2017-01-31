#!/bin/bash
#This test tests that self-heals don't perform fsync when durability is turned
#off

. $(dirname $0)/../include.rc
. $(dirname $0)/../traps.rc
. $(dirname $0)/../volume.rc

function count_processes {
	# It would generally be a good idea to use "pgrep -x" to ensure an
	# exact match, but the version of pgrep we have on NetBSD (a.k.a.
	# the worst operating system ever) doesn't support that option.
	# Fortunately, "glusterfsd" isn't the prefix of any other name,
	# so this works anyway.  For now.
	pgrep glusterfsd | wc -w
}

TEST glusterd
TEST $CLI volume set all cluster.brick-multiplex yes
push_trapfunc "$CLI volume set all cluster.brick-multiplex off"
push_trapfunc "cleanup"

# Create two vanilla volumes.
TEST $CLI volume create $V0 $H0:$B0/brick-${V0}-{0,1}
TEST $CLI volume create $V1 $H0:$B0/brick-${V1}-{0,1}

# Start both.
TEST $CLI volume start $V0
TEST $CLI volume start $V1

# There should be only one process for compatible volumes.  We can't use
# EXPECT_WITHIN here because it could transiently see one process as two are
# coming up, and yield a false positive.
sleep $PROCESS_UP_TIMEOUT
EXPECT "1" count_processes

# Make the second volume incompatible with the first.
TEST $CLI volume stop $V1
TEST $CLI volume set $V1 server.manage-gids no
TEST $CLI volume start $V1

# There should be two processes this time (can't share protocol/server).
EXPECT_WITHIN $PROCESS_UP_TIMEOUT "2" count_processes
