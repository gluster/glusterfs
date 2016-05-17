#!/bin/sh

##  Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com/>
##  This file is part of GlusterFS.
##
##  This file is licensed to you under your choice of the GNU Lesser
##  General Public License, version 3 or any later version (LGPLv3 or
##  later), or the GNU General Public License, version 2 (GPLv2), in all
##  cases as published by the Free Software Foundation.

BRICKPATH=    #Brick path of gluster volume
SLAVEHOST=    #Slave hostname
SLAVEVOL=     #Slave volume
SLAVEMNT=     #Slave gluster volume mount point
WORKERS=4     #Default number of worker threads

out()
{
    echo "$@";
}

fatal()
{
    out FATAL "$@";
    exit 1
}

ping_host ()
{
    ### Use bash internal socket support
    {
        exec 400<>/dev/tcp/$1/$2
        if [ $? -ne '0' ]; then
            return 1;
        else
            exec 400>&-
            return 0;
        fi
    } 1>&2 2>/dev/null
}

mount_slave()
{
    local i; # inode number
    SSH_PORT=22

    SLAVEMNT=`mktemp -d`
    [ "x$SLAVEMNT" = "x" ] && fatal "Could not mktemp directory";
    [ -d "$SLAVEMNT" ] || fatal "$SLAVEMNT not a directory";

    ping_host ${SLAVEHOST} $SSH_PORT
    if [ $? -ne 0 ]; then
        echo "$SLAVEHOST not reachable.";
        exit 1;
    fi;

    glusterfs --volfile-id=$SLAVEVOL --aux-gfid-mount --volfile-server=$SLAVEHOST $SLAVEMNT;
    i=$(stat -c '%i' $SLAVEMNT);
    [ "x$i" = "x1" ] || fatal "Could not mount volume $2 on $SLAVEMNT Please check host and volume exists";
}

parse_cli()
{
    if [[ $# -ne 4 ]]; then
        echo "Usage: gfind_missing_files <brick-path> <slave-host> <slave-vol> <OUTFILE>"
        exit 1
    else
        BRICKPATH=$1;
        SLAVEHOST=$2;
        SLAVEVOL=$3;
        OUTFILE=$4;

        mount_slave;
        echo "Slave volume is mounted at ${SLAVEMNT}"
        echo
    fi
}

main()
{
    parse_cli "$@";

    echo "Calling crawler...";
    path=$(readlink -e $0)
    $(dirname $path)/gcrawler ${BRICKPATH} ${SLAVEMNT} ${WORKERS} > ${OUTFILE}

    #Clean up the mount
    umount $SLAVEMNT;
    rmdir $SLAVEMNT;

    echo "Crawl Complete."
    num_files_missing=$(wc -l ${OUTFILE} | awk '{print $1}')
    if [ $num_files_missing -eq 0 ]
    then
        echo "Total Missing File Count : 0"
        exit 0;
    fi

    echo "gfids of skipped files are available in the file ${OUTFILE}"
    echo
    echo "Starting gfid to path conversion"

    #Call python script to convert gfids to full pathname
    INFILE=$(readlink -e ${OUTFILE})
    python $(dirname $path)/gfid_to_path.py ${BRICKPATH} ${INFILE} 1> ${OUTFILE}_pathnames 2> ${OUTFILE}_gfids
    echo "Path names of skipped files are available in the file ${OUTFILE}_pathnames"

    gfid_to_path_failures=$(wc -l ${OUTFILE}_gfids | awk '{print $1}')
    if [ $gfid_to_path_failures -gt 0 ]
    then
       echo "WARNING: Unable to convert some GFIDs to Paths, GFIDs logged to ${OUTFILE}_gfids"
       echo "Use $(dirname $path)/gfid_to_path.sh <brick-path> ${OUTFILE}_gfids to convert those GFIDs to Path"
    fi

    #Output
    echo "Total Missing File Count : $(wc -l ${OUTFILE} | awk '{print $1}')"
}

main "$@";
