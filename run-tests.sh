#!/bin/bash
# Copyright (c) 2013-2014 Red Hat, Inc. <http://www.redhat.com>
#

# As many tests are designed to take values of variables from 'env.rc',
# it is good to source the file. While it is also required to source the
# file individually in each tests (as it should be possible to run the
# tests separately), exporting variables from env.rc is not harmful if
# done here

source ./tests/env.rc

export TZ=UTC
force="no"
head="yes"
retry="yes"
tests=""
exit_on_failure="yes"
skip_bad_tests="yes"
skip_known_bugs="yes"
result_output="/tmp/gluster_regression.txt"
section_separator="========================================"
run_timeout=200
kill_after_time=5
nfs_tests=$RUN_NFS_TESTS

# Option below preserves log tarballs for each run of a test separately
#       named: <test>-iteration-<n>.tar
# If set to any other value, then log tarball is just named after the test and
# overwritten in each iteration (saves space)
#       named: <test>.tar
# Use option -p to override default behavior
skip_preserve_logs="yes"

OSTYPE=$(uname -s)

# Function for use in generating filenames with increasing "-<n>" index
# In:
#       $1 basepath: Directory where file needs to be created
#       $2 filename: Name of the file sans extension
#       $3 extension: Extension string that would be appended to the generated
#               filename
# Out:
#       string of next available filename with appended "-<n>"
# Example:
#       Interested routines that want to create a file name, say foo-<n>.txt at
#       location /var/log/gluster would pass in "/var/log/gluster" "foo" "txt"
#       and be returned next available foo-<n> filename to create.
# Notes:
#       Function will not accept empty extension, and will return the same name
#       over and over (which can be fixed when there is a need for it)
function get_next_filename()
{
        local basepath=$1
        local filename=$2
        local extension=$3
        local next=1
        local tfilename="${filename}-${next}"
        while [ -e "${basepath}/${tfilename}.${extension}" ]; do
                next=$((next+1))
                tfilename="${filename}-${next}"
        done

        echo "$tfilename"
}

# Tar the gluster logs and generate a tarball named after the first parameter
# passed in to the function. Ideally the test name is passed to this function
# to generate the required tarball.
# Tarball name is further controlled by the variable skip_preserve_logs
function tar_logs()
{
        t=$1

        logdir=$(gluster --print-logdir)
        basetarname=$(basename "$t" .t)

        if [ -n "$logdir" ]
        then
                if [[ $skip_preserve_logs == "yes" ]]; then
                        savetarname=$(get_next_filename "${logdir}" \
                                "${basetarname}-iteration" "tar" \
                                | tail -1)
                else
                        savetarname="$basetarname"
                fi

                # Can't use --exclude here because NetBSD doesn't have it.
                # However, both it and Linux have -X to take patterns from
                # a file, so use that.
                (echo '*.tar'; echo .notar) > "${logdir}"/.notar \
                        && \
                tar -cf "${logdir}"/"${savetarname}".tar -X "${logdir}"/.notar \
                        "${logdir}"/* 2> /dev/null \
                        && \
                find "$logdir"/* -maxdepth 0 -name '*.tar' -prune \
                                        -o -exec rm -rf '{}' ';'

                echo "Logs preserved in tarball $savetarname.tar"
        else
                echo "Logs not preserved, as logdir is not set"
        fi
}

function check_dependencies()
{
    ## Check all dependencies are present
    MISSING=""

    # Check for dbench
    env dbench --usage > /dev/null 2>&1
    if [ $? -ne 0 ]; then
        MISSING="$MISSING dbench"
    fi

    # Check for git
    env git --version > /dev/null 2>&1
    if [ $? -ne 0 ]; then
        MISSING="$MISSING git"
    fi

    # Check for nfs-utils (Linux-only: built-in NetBSD with different name)
    if [ "x`uname -s`" = "xLinux" ] ; then
      env mount.nfs -V > /dev/null 2>&1
      if [ $? -ne 0 ]; then
          MISSING="$MISSING nfs-utils"
      fi
    fi

    # Check for netstat
    env netstat --version > /dev/null 2>&1
    if [ $? -ne 0 ]; then
        MISSING="$MISSING netstat"
    fi

    # Check for the Perl Test Harness
    env prove --version > /dev/null 2>&1
    if [ $? -ne 0 ]; then
        MISSING="$MISSING perl-Test-Harness"
    fi

    which json_verify > /dev/null
    if [ $? -ne 0 ]; then
        MISSING="$MISSING json_verify"
    fi

    # Check for XFS programs (Linux Only: NetBSD does without)
    if [ "x`uname -s`" = "xLinux" ] ; then
      env mkfs.xfs -V > /dev/null 2>&1
      if [ $? -ne 0 ]; then
          MISSING="$MISSING xfsprogs"
      fi
    fi

    # Check for attr
    env getfattr --version > /dev/null 2>&1
    if [ $? -ne 0 ]; then
        MISSING="$MISSING attr"
    fi

    # Check for pidof
    pidof pidof > /dev/null 2>&1
    if [ $? -ne 0 ]; then
        MISSING="$MISSING pidof"
    fi

    # Check for netstat
    env netstat --version > /dev/null 2>&1
    if [ $? -ne 0 ]; then
        MISSING="$MISSING netstat"
    fi

    # check for psutil python package
    test `uname -s` == "Darwin" || test `uname -s` == "FreeBSD" && {
        pip show psutil | grep -q psutil >/dev/null 2>&1
        if [ $? -ne 0 ]; then
            MISSING="$MISSING psutil"
        fi
    }

    ## If dependencies are missing, warn the user and abort
    if [ "x$MISSING" != "x" ]; then
        test "x${force}" != "xyes" && echo "Aborting."
        echo
        echo "The following required tools are missing:"
        echo
        for pkg in $MISSING; do
            echo "  * $pkg"
        done
        echo
        test "x${force}" = "xyes" && return
        echo "Please install them and try again."
        echo
        exit 2
    fi
}

function check_location()
{
    regression_testsdir=$(dirname $0);

    if [ ! -f ${regression_testsdir}/tests/include.rc ]; then
        echo "Aborting."
        echo
        echo "The tests/ subdirectory seems to be missing."
        echo
        echo "Please correct the problem and try again."
        echo
        exit 1
    fi
}

function check_user()
{
    # If we're not running as root, warn the user and abort
    MYUID=`/usr/bin/id -u`
    if [ 0${MYUID} -ne 0 ]; then
        echo "Aborting."
        echo
        echo "The GlusterFS Test Framework must be run as root."
        echo
        echo "Please change to the root user and try again."
        echo
        exit 3
    fi
}

function match()
{
        # Patterns considered valid:
        # 0. Empty means everything
        #    ""           matches ** i.e all
        # 1. full or partial file/directory names
        #   basic         matches tests/basic
        #   basic/afr     matches tests/basic/afr
        # 2. globs
        #   basic/*       matches all files and directories in basic
        #   basic/*/      matches subdirectories in basic (afr|ec)
        # 3. numbered bug matching
        #   884455        matches bugs/bug-884455.t
        #   859927        matches bugs/859927, bugs/bug-859927.t
        #   1015990       matches /bugs/bug-1015990-rep.t, bug-1015990.t
        # ...lots of other cases accepted as well, since globbing is tricky.
        local t=$1
        shift
        local a
        local match=1
        if [ -z "$@" ]; then
            match=0
            return $match
        fi
        for a in $@ ; do
            case "$t" in
                *$a*)
                    match=0
                    ;;
            esac
        done
        return $match
}

# Tests can have comment lines with some comma separated values within them.
# Key names used to determine test status are
# G_TESTDEF_TEST_STATUS_CENTOS6
# G_TESTDEF_TEST_STATUS_NETBSD7
# Some examples:
# G_TESTDEF_TEST_STATUS_CENTOS6=BAD_TEST,BUG=123456
# G_TESTDEF_TEST_STATUS_CENTOS6=BRICK_MUX_BAD_TEST,BUG=123456
# G_TESTDEF_TEST_STATUS_NETBSD7=KNOWN_ISSUE,BUG=4444444
# G_TESTDEF_TEST_STATUS_CENTOS6=BAD_TEST,BUG=123456;555555
# G_TESTDEF_TEST_STATUS_CENTOS6=NFS_TESTS,BUG=1385758
# You can change status of test to enabled or delete the line only if all the
# bugs are closed or modified or if the patch fixes it.
function get_test_status ()
{
    local test_name=$1
    local host_os=""
    local result=""

    host_os=$(uname -s)

    case "$host_os" in
    # Leaving out the logic to determine the particular distro and version
    # for later. Why does the key have the distro and version then?
    # Because changing the key in all test files would be very big process
    # updating just this function with a better logic much simpler.
    Linux)
        result=$(grep -e "^#G_TESTDEF_TEST_STATUS_CENTOS6" $test_name | \
                 awk -F"," {'print $1'} | awk -F"=" {'print $2'}) ;;
    NetBSD)
        result=$(grep -e "^#G_TESTDEF_TEST_STATUS_NETBSD7" $test_name | \
                 awk -F"," {'print $1'} | awk -F"=" {'print $2'}) ;;
    *)
        result="ENABLED" ;;
    esac

    echo "$result"

}

function get_bug_list_for_disabled_test ()
{
    local test_name=$1
    local host_os=""
    local result=""

    host_os=$(uname -s)

    case "$host_os" in
    # Leaving out the logic to determine the particular distro and version
    # for later. Why does the key have the distro and version then?
    # Because changing the key in all test files would be very big process
    # updating just this function with a better logic much simpler.
    Linux)
        result=$(grep -e "^#G_TESTDEF_TEST_STATUS_CENTOS6" $test_name | \
                 awk -F"," {'print $2'} | awk -F"=" {'print $2'}) ;;
    NetBSD)
        result=$(grep -e "^#G_TESTDEF_TEST_STATUS_NETBSD7" $test_name | \
                 awk -F"," {'print $2'} | awk -F"=" {'print $2'}) ;;
    *)
        result="0000000" ;;
    esac

    echo "$result"

}

function run_tests()
{
    RES=0
    FLAKY=''
    FAILED=''
    TESTS_NEEDED_RETRY=''
    GENERATED_CORE=''
    total_tests=0
    selected_tests=0
    skipped_bad_tests=0
    skipped_known_issue_tests=0
    total_run_tests=0

    # key = path of .t file; value = time taken to run the .t file
    declare -A ELAPSEDTIMEMAP

    # Test if -k is supported for timeout command
    # This is not supported on centos6, but spuported on centos7
    # The flags is required for running the command in both flavors
    timeout_cmd_exists="yes"
    timeout -k 1 10 echo "testing 'timeout' command"
    if [ $? -ne 0 ]; then
        timeout_cmd_exists="no"
    fi

    all_tests=($(find ${regression_testsdir}/tests -name '*.t' | sort))
    all_tests_cnt=${#all_tests[@]}
    for t in "${all_tests[@]}" ; do
        old_cores=$(ls /*-*.core 2> /dev/null | wc -l)
        total_tests=$((total_tests+1))
        if match $t "$@" ; then
            selected_tests=$((selected_tests+1))
            echo
            echo $section_separator "(${total_tests} / ${all_tests_cnt})" $section_separator
            if [[ $(get_test_status $t) =~ "BAD_TEST" ]] && \
               [[ $skip_bad_tests == "yes" ]]
            then
                skipped_bad_tests=$((skipped_bad_tests+1))
                echo "Skipping bad test file $t"
                echo "Reason: bug(s):" $(get_bug_list_for_disabled_test $t)
                echo $section_separator$section_separator
                echo
                continue
            fi
            if [[ $(get_test_status $t) == "KNOWN_ISSUE" ]] && \
               [[ $skip_known_bugs == "yes" ]]
            then
                skipped_known_issue_tests=$((skipped_known_issue_tests+1))
                echo "Skipping test file $t due to known issue"
                echo "Reason: bug(s):" $(get_bug_list_for_disabled_test $t)
                echo $section_separator$section_separator
                echo
                continue
            fi
            if [[ $(get_test_status $t) == "NFS_TEST" ]] && \
               [[ $nfs_tests == "no" ]]
            then
                echo "Skipping nfs test file $t"
                echo $section_separator$section_separator
                echo
                continue
            fi
            total_run_tests=$((total_run_tests+1))
            echo "[$(date +%H:%M:%S)] Running tests in file $t"
            starttime="$(date +%s)"

            local cmd_timeout=$run_timeout;
            if [ ${timeout_cmd_exists} == "yes" ]; then
                if [ $(grep -c "SCRIPT_TIMEOUT=" ${t}) == 1 ] ; then
                    cmd_timeout=$(grep "SCRIPT_TIMEOUT=" ${t} | cut -f2 -d'=');
                    echo "Timeout set is ${cmd_timeout}, default ${run_timeout}"
                fi
                timeout --foreground -k ${kill_after_time} ${cmd_timeout} prove -vmfe '/bin/bash' ${t}
            else
                prove -vmfe '/bin/bash' ${t}
            fi
            TMP_RES=$?
            ELAPSEDTIMEMAP[$t]=`expr $(date +%s) - $starttime`
            tar_logs "$t"

            # timeout always return 124 if it is actually a timeout.
            if ((${TMP_RES} == 124)); then
                echo "${t} timed out after ${cmd_timeout} seconds"
            fi

            if [ ${TMP_RES} -ne 0 ]  && [ "x${retry}" = "xyes" ] ; then
                echo "$t: bad status $TMP_RES"
                echo ""
                echo "       *********************************"
                echo "       *       REGRESSION FAILED       *"
                echo "       * Retrying failed tests in case *"
                echo "       * we got some spurious failures *"
                echo "       *********************************"
                echo ""

                if [ ${timeout_cmd_exists} == "yes" ]; then
                    timeout --foreground -k ${kill_after_time} ${cmd_timeout} prove -vmfe '/bin/bash' ${t}
                else
                    prove -vmfe '/bin/bash' ${t}
                fi
                TMP_RES=$?
                tar_logs "$t"

                if ((${TMP_RES} == 124)); then
                    echo "${t} timed out after ${cmd_timeout} seconds"
                fi

                TESTS_NEEDED_RETRY="${TESTS_NEEDED_RETRY}${t} "
            fi


            if [ ${TMP_RES} -ne 0 ] ; then
		if [[ "$t" == *"tests/000-flaky/"* ]]; then
                    FLAKY="${FLAKY}${t} "
		    echo "FAILURE -> SUCCESS: Flaky test"
		    TMP_RES=0
		else
                    RES=${TMP_RES}
                    FAILED="${FAILED}${t} "
		fi
            fi

            new_cores=$(ls /*-*.core 2> /dev/null | wc -l)
            if [ x"$new_cores" != x"$old_cores" ]; then
                core_diff=$((new_cores-old_cores))
                echo "$t: $core_diff new core files"
                RES=1
                GENERATED_CORE="${GENERATED_CORE}${t} "
            fi
            echo "End of test $t"
            echo $section_separator$section_separator
            echo
            if [ $RES -ne 0 ] && [ x"$exit_on_failure" = "xyes" ] ; then
                break;
            fi
        fi
    done
    echo
    echo "Run complete"
    echo $section_separator$section_separator
    echo "Number of tests found:                             $total_tests"
    echo "Number of tests selected for run based on pattern: $selected_tests"
    echo "Number of tests skipped as they were marked bad:   $skipped_bad_tests"
    echo "Number of tests skipped because of known_issues:   $skipped_known_issue_tests"
    echo "Number of tests that were run:                     $total_run_tests"

    echo
    echo "Tests ordered by time taken, slowest to fastest: "
    echo $section_separator$section_separator
    for key in "${!ELAPSEDTIMEMAP[@]}"
    do
        echo "$key  -  ${ELAPSEDTIMEMAP["$key"]} second"
    done | sort -rn -k3

    # initialize the output file
    echo > "${result_output}"

    # Output the errors into a file
    if [ ${RES} -ne 0 ] ; then
        FAILED=$( echo ${FAILED} | tr ' ' '\n' | sort -u )
        FAILED_COUNT=$( echo -n "${FAILED}" | grep -c '^' )
        echo -e "\n$FAILED_COUNT test(s) failed \n${FAILED}" >> "${result_output}"
        GENERATED_CORE=$( echo  ${GENERATED_CORE} | tr ' ' '\n' | sort -u )
        GENERATED_CORE_COUNT=$( echo -n "${GENERATED_CORE}" | grep -c '^' )
        echo -e "\n$GENERATED_CORE_COUNT test(s) generated core \n${GENERATED_CORE}" >> "${result_output}"
        cat "${result_output}"
    fi
    TESTS_NEEDED_RETRY=$( echo ${TESTS_NEEDED_RETRY} | tr ' ' '\n' | sort -u )
    RETRY_COUNT=$( echo -n "${TESTS_NEEDED_RETRY}" | grep -c '^' )
    if [ ${RETRY_COUNT} -ne 0 ] ; then
        echo -e "\n${RETRY_COUNT} test(s) needed retry \n${TESTS_NEEDED_RETRY}" >> "${result_output}"
    fi

    FLAKY_TESTS_FAILED=$( echo ${FLAKY} | tr ' ' '\n' | sort -u )
    RETRY_COUNT=$( echo -n "${FLAKY_TESTS_FAILED}" | grep -c '^' )
    if [ ${RETRY_COUNT} -ne 0 ] ; then
        echo -e "\n${RETRY_COUNT} flaky test(s) marked as success even though they failed \n${FLAKY_TESTS_FAILED}" >> "${result_output}"
    fi

    echo
    echo "Result is $RES"
    echo
    return ${RES}
}

function run_head_tests()
{
    [ -d ${regression_testsdir}/.git ] || return 0

    # The git command needs $cwd to be within the repository, but run_tests
    # needs it to be back where we started.
    pushd $regression_testsdir
    git_cmd="git diff-tree --no-commit-id --name-only --diff-filter=ACMRTUXB"
    htests=$($git_cmd -r HEAD tests | grep '.t$')
    popd
    [ -n "$htests" ] || return 0

    # Perhaps it's not ideal that we'll end up re-running these tests, but the
    # gains from letting them fail fast in the first round should outweigh the
    # losses from running them again in the second.  OTOH, with so many of our
    # tests being non-deterministic, maybe it doesn't hurt to give the newest
    # tests an extra workout.
    run_tests "$htests"
}

function show_usage ()
{
    cat <<EOF
Usage: $0 <opts> [<glob>|<bzid>]...

Options:

-f  force
-h  skip tests altering from HEAD
-H  run only tests altering from HEAD
-r  retry failed tests
-R  do not retry failed tests
-c  dont't exit on failure
-b  don't skip bad tests
-k  don't skip known bugs
-p  don't keep logs from preceding runs
-o  OUTPUT
-t  TIMEOUT
-n  skip NFS tests
--help
EOF
}

usage="no"

function parse_args ()
{
    args=`getopt -u -l help frRcbkphHno:t: "$@"`
    if ! [ $? -eq 0 ]; then
	show_usage
	exit 1
    fi
    set -- $args
    while [ $# -gt 0 ]; do
        case "$1" in
        -f)    force="yes" ;;
        -h)    head="no" ;;
        -H)    head="only" ;;
        -r)    retry="yes" ;;
        -R)    retry="no" ;;
        -c)    exit_on_failure="no" ;;
        -b)    skip_bad_tests="no" ;;
        -k)    skip_known_bugs="no" ;;
        -p)    skip_preserve_logs="no" ;;
        -o)    result_output="$2"; shift;;
        -t)    run_timeout="$2"; shift;;
        -n)    nfs_tests="no";;
        --help) usage="yes" ;;
        --)    shift; break;;
        esac
        shift
    done
    tests="$@"
}


echo
echo ... GlusterFS Test Framework ...
echo

# Get user options
parse_args "$@"
if [ x"$usage" == x"yes" ]; then
    show_usage
    exit 0
fi

# Make sure we're running as the root user
check_user

# Make sure the needed programs are available
check_dependencies

# Check we're running from the right location
check_location

# Run the tests
if [ x"$head" != x"no" ]; then
        run_head_tests || exit 1
fi
if [ x"$head" != x"only" ]; then
        run_tests "$tests" || exit 1
fi
