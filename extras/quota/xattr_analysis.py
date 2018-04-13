#!/usr/bin/python2
# Below script has two purposes
#  1. Display xattr of entire FS tree in a human readable form
#  2. Display all the directory where contri and size mismatch.
#      (If there are any directory with contri and size mismatch that are not dirty
#       then that highlights a propogation issue)
#  The script takes only one input LOG _FILE generated from the command,
#  find <brick_path> | xargs  getfattr -d -m. -e hex  > log_gluster_xattr

from __future__ import print_function
import re
import subprocess
import sys
from hurry.filesize import size

if len(sys.argv) < 2:
    sys.exit('Usage: %s log_gluster_xattr \n'
              'to genereate log_gluster_xattr use: \n'
              'find <brick_path> | xargs  getfattr -d -m. -e hex  > log_gluster_xattr'
              % sys.argv[0])
LOG_FILE=sys.argv[1]

def get_quota_xattr_brick():
    out = subprocess.check_output (["/usr/bin/cat", LOG_FILE])
    pairs = out.splitlines()

    xdict = {}
    mismatch_size = [('====contri_size===', '====size====')]
    for xattr in pairs:
        k = xattr.split("=")[0]
        if re.search("# file:",k):
            print(xdict)
            filename=k
            print("=====" + filename + "=======")
            xdict = {}
        elif k is "":
            pass
        else:
            print(xattr)
            v = xattr.split("=")[1]
            if re.search("contri",k):
                if len(v) == 34:
                    # for files size is obtained in iatt, file count should be 1, dir count=0
                    xdict['contri_file_count'] = int(v[18:34], 16)
                    xdict['contri_dir_count'] = 0
                else:
                    xdict['contri_size'] = size(int(v[2:18], 16))
                    xdict['contri_file_count'] = int(v[18:34], 16)
                    xdict['contri_dir_count'] = int(v[34:], 16)
            elif re.search("size",k):
                xdict['size'] = size(int(v[2:18], 16))
                xdict['file_count'] = int(v[18:34], 16)
                xdict['dir_count'] = int(v[34:], 16)
            elif re.search("dirty",k):
                if v == '0x3000':
                    xdict['dirty'] = False
                elif v == '0x3100':
                    xdict['dirty'] = True
            elif re.search("limit_objects",k):
                xdict['limit_objects'] = int(v[2:18], 16)
            elif re.search("limit_set",k):
                xdict['limit_set'] = size(int(v[2:18], 16))

            if 'size' in xdict and 'contri_size' in xdict and xdict['size'] != xdict['contri_size']:
                mismatch_size.append((xdict['contri_size'], xdict['size'], filename))

    for values in mismatch_size:
        print(values)


if __name__ == '__main__':
    get_quota_xattr_brick()

