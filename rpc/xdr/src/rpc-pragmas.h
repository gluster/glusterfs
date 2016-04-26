/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef GLUSTERFS_RPC_PRAGMAS_H
#define GLUSTERFS_RPC_PRAGMAS_H

#if defined(__GNUC__)
#if __GNUC__ >= 4
#if !defined(__clang__)
#if !defined(__NetBSD__)
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif
#else
#pragma clang diagnostic ignored "-Wunused-variable"
#pragma clang diagnostic ignored "-Wunused-value"
#endif
#endif
#endif

#endif /* GLUSTERFS_RPC_PRAGMAS_H */
