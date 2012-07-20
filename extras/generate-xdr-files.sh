#!/bin/sh

_init ()
{
    xfile="$1";
    # TODO: check the validity of .x file

    cfile="${1%.x}.c";
    hfile="${1%.x}.h";

    tmp_cfile="$1.c";

    tmp1_hfile="$1.h.tmp";
    tmp1_cfile="$1.c.tmp";

}

append_licence_header ()
{
    src_file=$1;
    dst_file=$2;

    cat >$dst_file <<EOF
/*
  Copyright (c) 2007-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "xdr-common.h"
#include "compat.h"

#if defined(__GNUC__)
#if __GNUC__ >= 4
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif
#endif

EOF

    cat $src_file >> $dst_file;

}

main ()
{
    if [ $# -ne 1 ]; then
        echo "wrong number of arguments given"
        echo " $0 <XDR-definition-file>.x"
        exit 1;
    fi


    echo -n "writing the XDR routine file ($tmp_cfile) ...  ";
    rm -f $tmp_cfile;
    rpcgen -c -o $tmp_cfile $xfile;

    # get rid of warnings in xdr .c file due to "unused variable 'buf'"
    sed -i -e 's:buf;$:buf;\
        buf = NULL;:' $tmp_cfile;

    sed -i '/int i;/d' $tmp_cfile;

    echo "OK";

    # no need for a temporary file here as there are no changes from glusterfs
    echo -n "writing the XDR header file ($hfile) ...  ";
    rm -f $hfile;
    rpcgen -h -o $hfile $xfile;

    # the '#ifdef' part of file should be fixed
    sed -i -e 's/-/_/g' $hfile;

    echo "OK";

    echo -n "writing licence header to the generated files ... ";
    # Write header to temp file and append generated file
    append_licence_header $hfile $tmp1_hfile;
    append_licence_header $tmp_cfile $tmp1_cfile;
    echo "OK"

    # now move the destination file to actual original file
    echo -n "updating existing files ... ";
    mv $tmp1_hfile $hfile;
    mv $tmp1_cfile $cfile;

    # remove unwanted temporary files (if any)
    rm -f $tmp_cfile $tmp1_cfile $tmp1_hfile

    echo "OK"

}

_init "$@" && main "$@";
