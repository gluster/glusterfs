#!/usr/bin/bash

export PATH=/opt/csw/bin:/opt/csw/gcc4/bin:/usr/ccs/bin:/usr/ucb:/usr/sfw/bin:$PATH

cd source/
make distclean
./configure --disable-fuse-client --prefix=/opt/glusterfs;
make;

exit 0
