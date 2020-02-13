#!/usr/bin/python3
"""

Copyright (c) 2020 Red Hat, Inc. <http://www.redhat.com>
This file is part of GlusterFS.

This file is licensed to you under your choice of the GNU Lesser
General Public License, version 3 or any later version (LGPLv3 or
later), or the GNU General Public License, version 2 (GPLv2), in all
cases as published by the Free Software Foundation.

"""

import argparse
import errno
import os, sys
import shutil
from datetime import datetime

def find_htime_path(brick_path):
    dirs = []
    htime_dir = os.path.join(brick_path, '.glusterfs/changelogs/htime')
    for file in os.listdir(htime_dir):
        if os.path.isfile(os.path.join(htime_dir,file)) and file.startswith("HTIME"):
            dirs.append(os.path.join(htime_dir, file))
        else:
            raise FileNotFoundError("%s unavailable" % (os.path.join(htime_dir, file)))
    return dirs

def modify_htime_file(brick_path):
    htime_file_path_list = find_htime_path(brick_path)

    for htime_file_path in htime_file_path_list:
        changelog_path = os.path.join(brick_path, '.glusterfs/changelogs')
        temp_htime_path = os.path.join(changelog_path, 'htime/temp_htime_file')
        with open(htime_file_path, 'r') as htime_file, open(temp_htime_path, 'w') as temp_htime_file:
            #extract epoch times from htime file
            paths = htime_file.read().split("\x00")

            for pth in paths:
                epoch_no = pth.split(".")[-1]
                changelog = os.path.basename(pth)
                #convert epoch time to year, month and day
                if epoch_no != '':
                    date=(datetime.fromtimestamp(float(int(epoch_no))).strftime("%Y/%m/%d"))
                    #update paths in temp htime file
                    temp_htime_file.write("%s/%s/%s\x00" % (changelog_path, date, changelog))
                    #create directory in the format year/month/days
                    path = os.path.join(changelog_path, date)

                if changelog.startswith("CHANGELOG."):
                    try:
                        os.makedirs(path, mode = 0o600);
                    except OSError as exc:
                        if exc.errno == errno.EEXIST:
                            pass
                        else:
                            raise

                    #copy existing changelogs to new directory structure, delete old changelog files
                    shutil.copyfile(pth, os.path.join(path, changelog))
                    os.remove(pth)

        #rename temp_htime_file with htime file
        os.rename(htime_file_path, os.path.join('%s.bak'%htime_file_path))
        os.rename(temp_htime_path, htime_file_path)

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument('brick_path', help="This upgrade script, which is to be run on\
                         server side, takes brick path as the argument, \
                         updates paths inside htime file and alters the directory structure \
                         above the changelog files inorder to support new optimised format \
                         of the directory structure as per \
                         https://review.gluster.org/#/c/glusterfs/+/23733/")
    args = parser.parse_args()
    modify_htime_file(args.brick_path)
