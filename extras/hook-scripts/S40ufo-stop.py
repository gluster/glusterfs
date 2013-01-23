#!/usr/bin/env python

import os
from optparse import OptionParser

if __name__ == '__main__':
    # check if swift is installed
    try:
        from gluster.swift.common.Glusterfs import get_mnt_point, unmount
    except ImportError:
        import sys
        sys.exit("Openstack Swift does not appear to be installed properly");

    op = OptionParser(usage="%prog [options...]")
    op.add_option('--volname', dest='vol', type=str)
    op.add_option('--last', dest='last', type=str)
    (opts, args) = op.parse_args()


    mnt_point = get_mnt_point(opts.vol)
    if mnt_point:
        unmount(mnt_point)
    else:
        sys.exit("get_mnt_point returned none for mount point")
