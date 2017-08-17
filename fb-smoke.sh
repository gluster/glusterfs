#!/bin/bash

#
# Tests
#
DESIRED_TESTS="\
 tests/basic/*.t\
 tests/basic/afr/*.t\
 tests/basic/distribute/*.t\
 tests/basic/dht/*.t\
 tests/features/brick-min-free-space.t\
 "

KNOWN_FLAKY_TESTS="\
 tests/basic/afr/shd-autofix-nogfid.t\
 tests/basic/accept-v6v4.t\
 tests/basic/bd.t\
 tests/basic/fop-throttling.t\
 tests/basic/fops-sanity.t\
 tests/basic/mgmt_v3-locks.t\
 tests/basic/mount.t\
 tests/basic/pump.t\
 tests/basic/rpm.t\
 tests/basic/uss.t\
 tests/basic/volume-snapshot.t\
"

#
# Helpers
#
function elapsed_since {
    start=$1
    (("$SECONDS" - "$start"))
}

function flaky {
    local f
    for f in ${KNOWN_FLAKY_TESTS}; do
        if [ "$f" == "$1" ]; then
            return 0
        fi
    done
    return 1
}

function outfile {
    printf "/tmp/%s.out" "$(echo "$f" | tr / -)"
}

function exit_test {
    if [ "$STOP_ON_FAIL" -eq "1" ]; then
        print_result
        exit "$1"
    fi
}

function test {
    f=$1
    printf "%s" "$f"
    local start
    start=$SECONDS
    local out
    out=$(outfile "$f")

    for i in $(seq 1 "$ATTEMPT"); do
        DEBUG=1 timeout --foreground "$TEST_TIMEOUT" prove -v "$f" &> "$out.$i"

        if [ "$?" -eq "0" ]; then
            SUCCESS=$SUCCESS+1
            printf " PASS (%s s)\n" "$(elapsed_since $start)"
            rm -f "$out.$i"
            return 0
        else
            printf " %s" "($i/$ATTEMPT)"
        fi
    done

    if [[ $? -eq 124 || $? -eq 137 ]]; then
        FAILED_TESTS+=($f)
        FAIL=$FAIL+1
        printf " TIMEOUT (%s s)\n" "$(elapsed_since $start)"
        exit_test 1
    else
        FAILED_TESTS+=($f)
        FAIL=$FAIL+1
        printf " FAIL (%s s)\n" "$(elapsed_since $start)"
        exit_test 1
    fi
}

function flakytest {
    f=$1

    if [ "$SKIP_FLAKY" -eq "1" ]; then
        SKIP=$SKIP+1
    else
        printf "<flaky> "
        test "$f"
    fi
}

function print_result {
    echo
    echo "== RESULTS =="
    echo "TESTS    : $TOTAL"
    echo "SUCCESS  : $SUCCESS"
    echo "FAIL     : $FAIL"
    echo "SKIP     : $SKIP"

    if [ "$FAIL" -gt "0" ]; then
        echo
        echo "== FAILED TESTS =="
        echo "${FAILED_TESTS[@]}"
        echo
        echo "== LOGS =="
        "ls /tmp/*.out.*"
        echo
        echo "== END =="
    fi
}

function run_remote {
    if [ ! -d "$FBCODE" ]; then
        echo "fbcode does not exists. Please checkout fbcode"
        return 1
    fi

    local flags=''
    if [ "$VERBOSE" -eq "1" ]; then
        flags="$flags -v"
    fi

    if [ "$VALGRIND" -eq "1" ]; then
        flags="$flags --valgrind"
    fi

    if [ "$ASAN" -eq "1" ]; then
        flags="$flags --asan"
    fi

    "$FBCODE/storage/gluster/gluster-build/fb-gluster-test.py" $flags --tester --n "$N" --hosts "$REMOTE_HOSTS" --tests "$REMOTE_TESTS"
}

#
# Main
#
declare -i TOTAL=0
declare -i SUCCESS=0
declare -i FAIL=0
declare -i SKIP=0
declare -a FAILED_TESTS

TEST_TIMEOUT=${TEST_TIMEOUT:=300}
SKIP_FLAKY=${SKIP_FLAKY:=1}
STOP_ON_FAIL=${STOP_ON_FAIL:=0}
FBCODE=${FBCODE:="$HOME/fbsource/fbcode"}
N=${N:=0}
REMOTE_HOSTS=${REMOTE_HOSTS:="$(smcc ls-hosts -s gluster.build.ash | xargs)"}
REMOTE=${REMOTE:=0}
REMOTE_TESTS=${REMOTE_TESTS:='smoke'}
VERBOSE=${VERBOSE:=0}
VALGRIND=${VALGRIND:=0}
ASAN=${ASAN:=0}

if [ "$REMOTE" -eq "1" ]; then
    run_remote
    exit $?
fi

if [ "$SKIP_FLAKY" -eq "0" ]; then
    ATTEMPT=${ATTEMPT:=3}
else
    ATTEMPT=${ATTEMPT:=1}
fi

echo "== SETTINGS =="
echo "TEST_TIMEOUT = $TEST_TIMEOUT s"
echo "SKIP_FLAKY   = $SKIP_FLAKY"
echo "STOP_ON_FAIL = $STOP_ON_FAIL"
echo "ATTEMPT      = $ATTEMPT"
echo "REMOTE       = $REMOTE"
echo "FBCODE       = $FBCODE"
echo

# try cleaning up the environment
rm -f /tmp/*.out.* || true

# sanity check
if ! cmp -s ./glusterfsd/src/.libs/glusterfsd $(which glusterfsd)
then
  echo "Installed gluster does not match local, perhaps you ought make install?"
  exit 1
fi

echo "== TESTS =="
for f in ${DESIRED_TESTS}
do
    TOTAL=$TOTAL+1
    if flaky "$f"; then
        flakytest "$f"
    else
        test "$f"
    fi
done

print_result
exit $FAIL
