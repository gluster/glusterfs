#!/bin/bash

source ./extras/distributed-testing/distributed-test-env

N=0
TESTS='all'
FLAKY=$KNOWN_FLAKY_TESTS
BROKEN=$BROKEN_TESTS
TEST_TIMEOUT_S=900

FLAGS=""

function print_env {
    echo "Settings:"
    echo "N=$N"
    echo -e "-------\nHOSTS\n$HOSTS\n-------"
    echo -e "TESTS\n$TESTS\n-------"
    echo -e "SKIP\n$FLAKY $BROKEN\n-------"
    echo -e "TEST_TIMEOUT_S=$TEST_TIMEOUT_S s\n"
}

function cleanup {
    rm -f /tmp/test-*.log
}

function usage {
    echo "Usage: $0 [-h or --help] [-v or --verbose]
             [--all] [--flaky] [--smoke] [--broken]
             [--valgrind] [--asan] [--asan-noleaks]
             [--hosts <hosts>] [-n <parallelism>]
             [--tests <tests>]
             [--id-rsa <ssh private key>]
    "
}

function parse_args () {
    args=`getopt \
            -o hvn: \
            --long help,verbose,valgrind,asan,asan-noleaks,all,\
smoke,flaky,broken,hosts:,tests:,id-rsa:,test-timeout: \
            -n 'fb-remote-test.sh' --  "$@"`

    if [ $? != 0 ]; then
        echo "Error parsing getopt"
        exit 1
    fi

    eval set -- "$args"

    while true; do
        case "$1" in
            -h | --help) usage ; exit 1 ;;
            -v | --verbose) FLAGS="$FLAGS -v" ; shift ;;
            --valgrind) FLAGS="$FLAGS --valgrind" ; shift ;;
            --asan-noleaks) FLAGS="$FLAGS --asan-noleaks"; shift ;;
            --asan) FLAGS="$FLAGS --asan" ; shift ;;
            --hosts) HOSTS=$2; shift 2 ;;
            --tests) TESTS=$2; FLAKY= ; BROKEN= ; shift 2 ;;
            --test-timeout) TEST_TIMEOUT_S=$2; shift 2 ;;
            --all) TESTS='all' ; shift 1 ;;
            --flaky) TESTS=$FLAKY; FLAKY= ; shift 1 ;;
            --smoke) TESTS=$SMOKE_TESTS; shift 1 ;;
            --broken) TESTS=$BROKEN_TESTS; FLAKY= ; BROKEN= ; shift 1 ;;
            --id-rsa) FLAGS="$FLAGS --id-rsa $2" ; shift 2 ;;
            -n) N=$2; shift 2 ;;
            *) break ;;
            esac
        done
        run_tests_args="$@"
}

function main {
    parse_args "$@"

    if [ -z "$HOSTS" ]; then
        echo "Please provide hosts to run the tests in"
	exit -1
    fi

    print_env

    cleanup

    "extras/distributed-testing/distributed-test-runner.py" $FLAGS --tester \
        --n "$N" --hosts "$HOSTS" --tests "$TESTS" \
        --flaky_tests "$FLAKY $BROKEN" --test-timeout "$TEST_TIMEOUT_S"

    exit $?
}

main "$@"
