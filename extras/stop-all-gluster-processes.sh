#! /bin/sh

function main()
{
    errors=0;

    for pidfile in $(find /var/lib/glusterd/ -iname '*pid');
    do
        pid=$(cat ${pidfile});
        echo "sending SIGTERM to process $pid";
        kill -TERM $pid;
    done

    # for geo-replication, only 'monitor' has pid file written, other
    # processes are not having a pid file, so get it through 'ps' and
    # handle these processes
    gsyncpid=`ps aux | grep gluster | grep gsync | awk '{print $2}'`;
    if [ -n "$gsyncpid" ]
    then
        kill -TERM $gsyncpid || errors=$(($errors + 1));
    fi

    sleep 5;

    # if pid file still exists, its something to KILL
    for pidfile in $(find /var/lib/glusterd/ -iname '*pid');
    do
        pid=$(cat ${pidfile});
        echo "sending SIGKILL to process $pid";
        kill -KILL $pid;
    done

    # handle 'KILL' of geo-replication
    gsyncpid=`ps aux | grep gluster | grep gsync | awk '{print $2}'`;
    if [ -n "$gsyncpid" ]
    then
        kill -KILL $gsyncpid || errors=$(($errors + 1));
    fi

    exit $errors;
}

main "$@";
