#!/bin/bash

source ./test_env

FBCODE="$HOME/fbsource/fbcode"
N=0
HOSTS=$(smcc ls-hosts -s gluster.build.ash gluster.build.prn | xargs)
TESTS=$DESIRED_TESTS
FLAKY=$KNOWN_FLAKY_TESTS

FLAGS=""

function print_env {
    echo "Settings:"
    echo "FBCODE=$FBCODE"
    echo "N=$N"
    echo -e "-------\nHOSTS\n$HOSTS\n-------"
    echo -e "TESTS\n$TESTS\n-------"
    echo -e "FLAKY\n$FLAKY\n-------"
}

function cleanup {
    rm -f /tmp/test-*.log
}

function usage {
    echo "Usage: $0 [-h or --help] [-v or --verbose]
             [--fbcode <fbcode root>]
             [--valgrind] [--asan] [--asan-noleaks]
             [--hosts <hosts>] [--smc-tier <tier name>] [-n <parallelism>]
             [--tests <tests>] [--flaky <tests>]
    "
}

function tiers_to_hosts {
    hosts=""
    for t in $1; do
        hosts="$hosts $(smcc ls-hosts -s $t | xargs)"
    done
    echo $hosts
}

function parse_args () {
    args=`getopt \
            -o hvn: \
            --long help,verbose,valgrind,asan,asan-noleaks,fbcode:,hosts:,smc-tier:,tests:,flaky: \
            -n 'fb-remote-test.sh' --  "$@"`

    if [ $? != 0 ]; then
        echo "Error parsing getopt"
        exit 1
    fi

    eval set -- "$args"

    while true; do
        case "$1" in
            -h | --help) usage ; exit 1 ;;
            --fbcode) FBCODE=$2 ; shift 2 ;;
            -v | --verbose) FLAGS="$FLAGS -v" ; shift ;;
            --valgrind) FLAGS="$FLAGS --valgrind" ; shift ;;
            --asan-noleaks) FLAGS="$FLAGS --asan-noleaks"; shift ;;
            --asan) FLAGS="$FLAGS --asan" ; shift ;;
            --hosts) HOSTS=$2; shift 2 ;;
            --smc-tier) HOSTS=$(tiers_to_hosts $2) ; shift 2 ;;
            --tests) TESTS=$2; shift 2 ;;
            --flaky) FLAKY=$2; shift 2 ;;
            -n) N=$2; shift 2 ;;
            *) break ;;
            esac
        done
        run_tests_args="$@"
}

function main {
    parse_args "$@"

    if [ ! -d "$FBCODE" ]; then
        echo "fbcode does not exists. Please checkout fbcode"
        return 1
    fi

    print_env

    cleanup

    "$FBCODE/storage/gluster/gluster-build/fb-gluster-test.py" $FLAGS --tester \
        --n "$N" --hosts "$HOSTS" --tests "$TESTS" --flaky_tests "$FLAKY"

    exit $?
}

main "$@"
