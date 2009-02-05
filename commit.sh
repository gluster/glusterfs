#!/bin/sh

export EDITOR="emacs"
#TLA_REVISION=$(expr 1 + $(cat ./libglusterfs/src/revision.h | cut -f 8 -d '-' | sed -e 's/"//'))
#sed -i "s/AC_INIT.*/AC_INIT([glusterfs],[2.0.0tla${TLA_REVISION}],[gluster-users@gluster.org])/g" ./configure.ac
tla commit --write-revision ./libglusterfs/src/revision.h:'#define GLUSTERFS_REPOSITORY_REVISION "%s"' "$@"
