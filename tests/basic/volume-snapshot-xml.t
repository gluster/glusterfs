#!/bin/bash

. $(dirname $0)/../include.rc
. $(dirname $0)/../snapshot.rc

function get-xml()
{
        $CLI $1 --xml | xmllint --format - | grep $2 | sed 's/\(<"$2">\|<\/"$2">\)//g'
}

cleanup;
TEST verify_lvm_version;
TEST glusterd;
TEST pidof glusterd;

TEST setup_lvm 1

TEST $CLI volume create $V0 $H0:$L1
TEST $CLI volume start $V0

# Snapshot config xmls
EXPECT "enable" get-xml "snapshot config activate-on-create enable" "activateOnCreate"
EXPECT "100" get-xml "snapshot config $V0 snap-max-hard-limit 100" "newHardLimit"
EXPECT "70" get-xml "snapshot config snap-max-soft-limit 70" "newSoftLimit"
EXPECT "enable" get-xml "snapshot config auto-delete enable" "autoDelete"

# Snapshot create, activate, deactivate xmls
EXPECT "snap1" get-xml "snapshot create snap1 $V0 no-timestamp" "name"
EXPECT "snap1" get-xml "snapshot deactivate snap1" "name"
EXPECT "snap1" get-xml "snapshot activate snap1" "name"
EXPECT "snap2" get-xml "snapshot create snap2 $V0 no-timestamp" "name"

# Snapshot info xmls
EXPECT "2" get-xml "snapshot info" "count"
EXPECT "Started" get-xml "snapshot info" "status"
EXPECT "2" get-xml "snapshot info volume $V0" "count"
EXPECT "Started" get-xml "snapshot info volume $V0" "status"
EXPECT "1" get-xml "snapshot info snap1" "count"
EXPECT "2" get-xml "snapshot info snap1" "snapCount"
EXPECT "Started" get-xml "snapshot info snap1" "status"

# Snapshot list xmls
EXPECT "2" get-xml "snapshot list" "count"
EXPECT "snap2" get-xml "snapshot list $V0" "snapshot"

# Snapshot status xmls
EXPECT "snap2" get-xml "snapshot status" "name"
EXPECT "snap2" get-xml "snapshot deactivate snap2" "name"
#XPECT "N/A" get-xml "snapshot status" "pid"
EXPECT "snap1" get-xml "snapshot status snap1" "name"
EXPECT "Yes" get-xml "snapshot status snap1" "brick_running"

# Snapshot restore xmls
TEST $CLI volume stop $V0
EXPECT "snap2" get-xml "snapshot restore snap2" "name"
EXPECT "30807" get-xml "snapshot restore snap2" "opErrno"
EXPECT "0" get-xml "snapshot restore snap1" "opErrno"

# Snapshot delete xmls
TEST $CLI volume start $V0 force
EXPECT "snap1" get-xml "snapshot create snap1 $V0 no-timestamp" "name"
EXPECT "snap2" get-xml "snapshot create snap2 $V0 no-timestamp" "name"
EXPECT "snap3" get-xml "snapshot create snap3 $V0 no-timestamp" "name"
EXPECT "Success" get-xml "snapshot delete snap3" "status"
EXPECT "Success" get-xml "snapshot delete all" "status"
EXPECT "0" get-xml "snapshot list" "count"
#XPECT "snap1" get-xml "snapshot create snap1 $V0 no-timestamp" "name"
#XPECT "snap2" get-xml "snapshot create snap2 $V0 no-timestamp" "name"
#XPECT "snap3" get-xml "snapshot create snap3 $V0 no-timestamp" "name"
#XPECT "Success" get-xml "snapshot delete volume $V0" "status"
#XPECT "0" get-xml "snapshot list" "count"

# Snapshot clone xmls
# Snapshot clone xml is broken. Once it is fixed it will be added here.

cleanup;
