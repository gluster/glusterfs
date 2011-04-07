#!/bin/bash

# This script is used to cleanup xattrs setup by quota-translator in (a)
# backend directory(ies). It takes a single or list of backend directories
# as argument(s).

usage ()
{
    echo >&2 "usage: $0 <list-of-backend-directories>"
}

main ()
{
    [ $# -lt 1 ] && usage

    INSTALL_DIR=`dirname $0`

    for i in $@; do
        find $i -exec $INSTALL_DIR/quota-remove-xattr.sh '{}' \;
    done
    
}

main $@
