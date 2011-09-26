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
  Copyright (c) 2007-2011 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/

#include "xdr-common.h"
#include "compat.h"

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
    sed -i -e 's:\(.*\)-\(.*\)_H_RPCGEN:\1_\2_H_RPCGEN:g' $hfile;

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
