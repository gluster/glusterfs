#!/usr/bin/python
# Copyright (c) 2015 Red Hat, Inc.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
#

from setuptools import setup, find_packages

version = '0.1.0'
name = 'distaflibs-gluster'

setup(
    name=name,
    version=version,
    description='DiSTAF Gluster Library',
    license='GPLv2+',
    author='Red Hat, Inc.',
    author_email='gluster-devel@gluster.org',
    url='http://www.gluster.org',
    packages=find_packages(),
    classifiers=[
        'Development Status :: 4 - Beta'
        'Environment :: Console'
        'Intended Audience :: Developers'
        'License :: OSI Approved :: GNU General Public License v2 or later (GPLv2+)'
        'Operating System :: POSIX :: Linux'
        'Programming Language :: Python'
        'Programming Language :: Python :: 2'
        'Programming Language :: Python :: 2.6'
        'Programming Language :: Python :: 2.7'
        'Topic :: Software Development :: Testing'
    ],
    install_requires=['distaf'],
    namespace_packages = ['distaflibs']
)
