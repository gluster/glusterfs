#!/usr/bin/python
# Copyright (c) 2012 Red Hat, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
# implied.
# See the License for the specific language governing permissions and
# limitations under the License.

from setuptools import setup, find_packages

from gluster.swift import __canonical_version__ as version


name = 'gluster_swift_ufo'


setup(
    name=name,
    version=version,
    description='Gluster Swift/UFO',
    license='Apache License (2.0)',
    author='Red Hat, Inc.',
    author_email='gluster-users@gluster.org',
    url='https://gluster.org/',
    packages=find_packages(exclude=['test', 'bin']),
    test_suite='nose.collector',
    classifiers=[
        'Development Status :: 4 - Beta',
        'License :: OSI Approved :: Apache Software License',
        'Operating System :: POSIX :: Linux',
        'Programming Language :: Python :: 2.6',
        'Environment :: No Input/Output (Daemon)',
        ],
    install_requires=[],  # removed for better compat
    scripts=[
        'bin/gluster-swift-gen-builders',
    ],
    entry_points={
        'paste.app_factory': [
            'proxy=gluster.swift.proxy.server:app_factory',
            'object=gluster.swift.obj.server:app_factory',
            'container=gluster.swift.container.server:app_factory',
            'account=gluster.swift.account.server:app_factory',
            ],
        'paste.filter_factory': [
            'gluster=gluster.swift.common.middleware.gluster:filter_factory',
            ],
        },
    )
