#!/bin/bash
#
# Run glfs_sysrq, a gfapi applications calling all glfs_sysrq() commands.
# Each command generates a specific log message, or something else that can be
# tested for existance.
#

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup

TEST glusterd
TEST pidof glusterd
TEST $CLI volume create $V0 $H0:$B0/brick1
EXPECT 'Created' volinfo_field $V0 'Status'

TEST $CLI volume start $V0;
EXPECT 'Started' volinfo_field $V0 'Status'

logdir=$(gluster --print-logdir)

# clear all statedumps
cleanup_statedump
TEST ! test -e $statedumpdir/*.dump.*
# vim friendly command */

build_tester $(dirname $0)/glfs_sysrq.c -lgfapi
TEST $(dirname $0)/glfs_sysrq $H0 $V0 $logdir/glfs_sysrq.log

# check for the help message in the log
TEST grep -q '"(H)elp"' $logdir/glfs_sysrq.log

# see if there is a statedump
TEST test -e $statedumpdir/*.dump.*
# vim friendly command */

cleanup_tester $(dirname $0)/glfs_sysrq
cleanup
