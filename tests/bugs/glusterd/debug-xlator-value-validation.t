#!/bin/bash
#
# debug.trace / debug.error-gen / debug.delay-gen take the *name* of the
# translator to insert the debug xlator above (a brick-graph xlator such as
# "posix" or "locks", or "client"), or "off" to disable -- not a boolean.
# Before the fix, glusterd had no validator on these keys, so
# "volume set <vol> debug.delay-gen on" (or any non-xlator string) returned
# success, stored the bogus value, and silently did nothing. Assert the invalid
# values are now rejected and the valid xlator targets still work and take
# effect.

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/${V0}0
TEST $CLI volume start $V0

# invalid values must be rejected (a boolean "on" is the trap that motivated this)
TEST ! $CLI volume set $V0 debug.delay-gen on
TEST ! $CLI volume set $V0 debug.delay-gen bogusvalue
TEST ! $CLI volume set $V0 debug.trace enable
TEST ! $CLI volume set $V0 debug.error-gen 1

# a rejected set must not be stored
EXPECT '' echo $($CLI volume info $V0 | grep -E 'debug\.(delay-gen|trace|error-gen): ')

# valid xlator targets must succeed
TEST $CLI volume set $V0 debug.delay-gen posix
TEST $CLI volume set $V0 debug.trace client
TEST $CLI volume set $V0 debug.error-gen locks

# and actually take effect: delay-gen inserted above posix in the brick volfile
brick_vol=$(ls $GLUSTERD_WORKDIR/vols/$V0/$V0.*${V0}0.vol 2>/dev/null | head -1)
TEST [ -n "$brick_vol" ]
EXPECT_NOT "0" echo $(grep -c 'debug/delay-gen' "$brick_vol")

# "off" (disable) must be accepted
TEST $CLI volume set $V0 debug.delay-gen off

cleanup;
