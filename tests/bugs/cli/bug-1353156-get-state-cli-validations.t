#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc
. $(dirname $0)/../../snapshot.rc
. $(dirname $0)/../../traps.rc

cleanup;

ODIR="/var/tmp/gdstates/"
NOEXDIR="/var/tmp/gdstatesfoo/"

function get_daemon_not_supported_part {
        echo $1
}

function get_usage_part {
        echo $7
}

function get_directory_doesnt_exist_part {
        echo $1
}

function get_parsing_arguments_part {
        echo $1
}

function positive_test {
	local text=$("$@")
	echo $text > /dev/stderr
	(echo -n $text | grep -qs ' state dumped to ') || return 1
	local opath=$(echo -n $text | awk '{print $5}')
	[ -r $opath ] || return 1
	rm -f $opath
}

TEST glusterd
TEST pidof glusterd
TEST mkdir -p $ODIR

push_trapfunc rm -rf $ODIR

TEST $CLI volume create $V0 disperse $H0:$B0/b1 $H0:$B0/b2 $H0:$B0/b3
TEST $CLI volume start $V0
TEST $CLI volume tier $V0 attach replica 2 $H0:$B1/b4 $H0:$B1/b5

TEST setup_lvm 1
TEST $CLI volume create $V1 $H0:$L1;
TEST $CLI volume start $V1

TEST $CLI snapshot create ${V1}_snap $V1

TEST positive_test $CLI get-state

TEST positive_test $CLI get-state glusterd

TEST ! $CLI get-state glusterfsd;
ERRSTR=$($CLI get-state glusterfsd 2>&1 >/dev/null);
EXPECT 'glusterd' get_daemon_not_supported_part $ERRSTR;
EXPECT 'Usage:' get_usage_part $ERRSTR;

TEST positive_test $CLI get-state file gdstate

TEST positive_test $CLI get-state glusterd file gdstate

TEST ! $CLI get-state glusterfsd file gdstate;
ERRSTR=$($CLI get-state glusterfsd file gdstate 2>&1 >/dev/null);
EXPECT 'glusterd' get_daemon_not_supported_part $ERRSTR;
EXPECT 'Usage:' get_usage_part $ERRSTR;

TEST positive_test $CLI get-state odir $ODIR

TEST positive_test $CLI get-state glusterd odir $ODIR

TEST positive_test $CLI get-state odir $ODIR file gdstate

TEST positive_test $CLI get-state glusterd odir $ODIR file gdstate

TEST positive_test $CLI get-state detail

TEST positive_test $CLI get-state glusterd detail

TEST positive_test $CLI get-state odir $ODIR detail

TEST positive_test $CLI get-state glusterd odir $ODIR detail

TEST positive_test $CLI get-state glusterd odir $ODIR file gdstate detail

TEST positive_test $CLI get-state volumeoptions

TEST positive_test $CLI get-state glusterd volumeoptions

TEST positive_test $CLI get-state odir $ODIR volumeoptions

TEST positive_test $CLI get-state glusterd odir $ODIR volumeoptions

TEST positive_test $CLI get-state glusterd odir $ODIR file gdstate volumeoptions

TEST ! $CLI get-state glusterfsd odir $ODIR;
ERRSTR=$($CLI get-state glusterfsd odir $ODIR 2>&1 >/dev/null);
EXPECT 'glusterd' get_daemon_not_supported_part $ERRSTR;
EXPECT 'Usage:' get_usage_part $ERRSTR;

TEST ! $CLI get-state glusterfsd odir $ODIR file gdstate;
ERRSTR=$($CLI get-state glusterfsd odir $ODIR file gdstate 2>&1 >/dev/null);
EXPECT 'glusterd' get_daemon_not_supported_part $ERRSTR;
EXPECT 'Usage:' get_usage_part $ERRSTR;

TEST ! $CLI get-state glusterfsd odir $NOEXDIR file gdstate;
ERRSTR=$($CLI get-state glusterfsd odir $NOEXDIR file gdstate 2>&1 >/dev/null);
EXPECT 'glusterd' get_daemon_not_supported_part $ERRSTR;
EXPECT 'Usage:' get_usage_part $ERRSTR;

TEST ! $CLI get-state odir $NOEXDIR;
ERRSTR=$($CLI get-state odir $NOEXDIR 2>&1 >/dev/null);
EXPECT 'Failed' get_directory_doesnt_exist_part $ERRSTR;

TEST ! $CLI get-state odir $NOEXDIR file gdstate;
ERRSTR=$($CLI get-state odir $NOEXDIR 2>&1 >/dev/null);
EXPECT 'Failed' get_directory_doesnt_exist_part $ERRSTR;

TEST ! $CLI get-state foo bar;
ERRSTR=$($CLI get-state foo bar 2>&1 >/dev/null);
EXPECT 'glusterd' get_daemon_not_supported_part $ERRSTR;
EXPECT 'Usage:' get_usage_part $ERRSTR;

TEST ! $CLI get-state glusterd foo bar;
ERRSTR=$($CLI get-state glusterd foo bar 2>&1 >/dev/null);
EXPECT 'Problem' get_parsing_arguments_part $ERRSTR;

TEST ! $CLI get-state glusterd detail file gdstate;
ERRSTR=$($CLI get-state glusterd foo bar 2>&1 >/dev/null);
EXPECT 'Problem' get_parsing_arguments_part $ERRSTR;

TEST ! $CLI get-state glusterd foo bar detail;
ERRSTR=$($CLI get-state glusterd foo bar 2>&1 >/dev/null);
EXPECT 'Problem' get_parsing_arguments_part $ERRSTR;

TEST ! $CLI get-state glusterd volumeoptions file gdstate;
ERRSTR=$($CLI get-state glusterd foo bar 2>&1 >/dev/null);
EXPECT 'Problem' get_parsing_arguments_part $ERRSTR;

TEST ! $CLI get-state glusterd foo bar volumeoptions;
ERRSTR=$($CLI get-state glusterd foo bar 2>&1 >/dev/null);
EXPECT 'Problem' get_parsing_arguments_part $ERRSTR;

cleanup;
