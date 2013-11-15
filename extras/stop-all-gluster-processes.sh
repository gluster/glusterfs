#! /bin/sh

function main()
{
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
    test -n $gsyncpid && kill -TERM $gsyncpid;

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
    test -n $gsyncpid && kill -KILL $gsyncpid;
}

main "$@";
