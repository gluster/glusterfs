#!/usr/bin/env bash

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

main()
{
    kill_mounts TERM
    kill_bricks_and_services TERM
    kill_georep_gsync TERM

    sleep 5;
    echo ""

    # still not Terminated? let's pass SIGKILL
    kill_mounts KILL
    kill_bricks_and_services KILL
    kill_georep_gsync KILL

    exit ${errors};
}

main
