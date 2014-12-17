#
# Copyright (c) 2011-2014 Red Hat, Inc. <http://www.redhat.com>
# This file is part of GlusterFS.

# This file is licensed to you under your choice of the GNU Lesser
# General Public License, version 3 or any later version (LGPLv3 or
# later), or the GNU General Public License, version 2 (GPLv2), in all
# cases as published by the Free Software Foundation.
#

"""
This setup.py only used to run tests, since geo-replication will
be installed in /usr/local/libexec/glusterfs or /usr/libexec/glusterfs
"""
from setuptools import setup

name = 'syncdaemon'

setup(
    name=name,
    version="",
    description='GlusterFS Geo Replication',
    license='',
    author='Red Hat, Inc.',
    author_email='gluster-devel@gluster.org',
    url='http://www.gluster.org',
    packages=['syncdaemon', ],
    test_suite='nose.collector',
    install_requires=[],
    scripts=[],
    entry_points={},
)
