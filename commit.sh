#!/bin/sh

export EDITOR=emacs
tla commit --write-revision ./libglusterfs/src/revision.h:'#define GLUSTERFS_REPOSITORY_REVISION "%s"'
