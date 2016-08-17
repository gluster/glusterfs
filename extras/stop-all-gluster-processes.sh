#!/usr/bin/env bash
#
# Kill all the processes/services except glusterd
#
# Usage: ./extras/stop-all-gluster-processes.sh [-g] [-h]
#    options:
#    -g  Terminate in graceful mode
#    -h  Show this message, then exit
#
# eg:
#  1. ./extras/stop-all-gluster-processes.sh
#  2. ./extras/stop-all-gluster-processes.sh -g
#
# By default, this script executes in force mode, i.e. all of brick, gsyncd
# and other glustershd services/processes are killed without checking for
# ongoing tasks such as geo-rep, self-heal, rebalance and etc. which may lead
# to inconsistency after the node is brought back.
#
# On specifying '-g' option this script works in graceful mode, to maintain
# data consistency the script fails with a valid exit code incase if any of
# the gluster processes are busy in doing their jobs.
#
# The author of page [1] proposes user-defined exit codes to the range 64 - 113
# Find the better explanation behind the choice in the link
#
# The exit code returned by stop-all-gluster-processes.sh:
#   0       No errors/Success
#   64      Rebalance is in progress
#   65      Self-Heal is in progress
#   66      Tier daemon running on this node
#   127     option not found
#
# [1] http://www.tldp.org/LDP/abs/html/exitcodes.html


# global
errors=0

# find the mounts and return their pids
get_mount_pids()
{
    local opts
    local pid

    for opts in $(grep -w fuse.glusterfs /proc/mounts| awk '{print $1":/"$2}');
    do
        IFS=' ' read -r -a volinfo <<< $(echo "${opts}" | sed 's/:\// /g')
        pid+="$(ps -Ao pid,args | grep -w "volfile-server=${volinfo[0]}" |
                grep -w "volfile-id=/${volinfo[1]}" | grep -w "${volinfo[2]}" |
                awk '{print $1}') "
    done
    echo "${pid}"
}

# handle mount processes i.e. 'glusterfs'
kill_mounts()
{
    local signal=${1}
    local pid

    for pid in $(get_mount_pids);
    do
        echo "sending SIG${signal} to mount process with pid: ${pid}";
        kill -${signal} ${pid};
    done
}

# handle brick processes and node services
kill_bricks_and_services()
{
    local signal=${1}
    local pidfile
    local pid

    for pidfile in $(find /var/lib/glusterd/ -name '*.pid');
    do
        local pid=$(cat ${pidfile});
        echo "sending SIG${signal} to pid: ${pid}";
        kill -${signal} ${pid};
    done
}

# for geo-replication, only 'monitor' has pid file written, other
# processes are not having a pid file, so get it through 'ps' and
# handle these processes
kill_georep_gsync()
{
    local signal=${1}

    # FIXME: add strick/better check
    local gsyncpid=$(ps -Ao pid,args | grep gluster | grep gsync |
                     awk '{print $1}');
    if [ -n "${gsyncpid}" ]
    then
        echo "sending SIG${signal} to geo-rep gsync process ${gsyncpid}";
        kill -${signal} ${gsyncpid} || errors=$((${errors} + 1));
    fi
}

# check if all processes are ready to die
check_background_tasks()
{
    volumes=$(gluster vol list)
    quit=0
    for volname in ${volumes};
    do
        # tiering
        if [[ $(gluster volume tier ${volname} status 2> /dev/null |
                grep "localhost" | grep -c "in progress") -gt 0 ]]
        then
            quit=66
            break;
        fi

        # rebalance
        if [[ $(gluster volume rebalance ${volname} status 2> /dev/null |
                grep -c "in progress") -gt 0 ]]
        then
            quit=64
            break;
        fi

        # self heal
        if [[ $(gluster volume heal ${volname} info | grep "Number of entries" |
                awk '{ sum+=$4} END {print sum}') -gt 0 ]];
        then
            quit=65
            break;
        fi

        # geo-rep, snapshot and quota doesn't need grace checks,
        # as they ensures the consistancy on force kills
    done

    echo ${quit}
}

usage()
{
    cat <<EOM
Usage: $0 [-g] [-h]
    options:
    -g  Terminate in graceful mode
    -h  Show this message, then exit

eg:
 1. $0
 2. $0 -g
EOM
}

main()
{
    while getopts "gh" opt; do
        case $opt in
            g)
                # graceful mode
                quit=$(check_background_tasks)
                if [[ ${quit} -ne 0 ]]
                then
                    exit ${quit};
                fi
                # else safe to kill
                ;;
            h)
                usage
                exit 0;
                ;;
            *)
                usage
                exit 127;
                ;;
        esac
    done
    # remove all the options that have been parsed by getopts
    shift $((OPTIND-1))

    kill_mounts TERM
    kill_georep_gsync TERM
    kill_bricks_and_services TERM

    sleep 5;
    echo ""

    # still not Terminated? let's pass SIGKILL
    kill_mounts KILL
    kill_georep_gsync KILL
    kill_bricks_and_services KILL

    exit ${errors};
}

main "$@"
