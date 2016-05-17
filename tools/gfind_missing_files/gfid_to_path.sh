#!/bin/sh

## Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com/>
## This file is part of GlusterFS.
##
## This file is licensed to you under your choice of the GNU Lesser
## General Public License, version 3 or any later version (LGPLv3 or
## later), or the GNU General Public License, version 2 (GPLv2), in all
## cases as published by the Free Software Foundation.

E_BADARGS=65


gfid_to_path ()
{
    brick_dir=$1;
    gfid_file=$(readlink -e $2);

    current_dir=$(pwd);
    cd $brick_dir;

    while read gfid
    do
        to_search=`echo .glusterfs/${gfid:0:2}"/"${gfid:2:2}"/"$gfid`;
        find . -samefile $to_search | grep -v $to_search;
    done < $gfid_file;

    cd $current_dir;
}


main ()
{
    if [ $# -ne 2 ]
    then
        echo "Usage: `basename $0` BRICK_DIR GFID_FILE";
        exit $E_BADARGS;
    fi

    gfid_to_path $1 $2;
}

main "$@";
