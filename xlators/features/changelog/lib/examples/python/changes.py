#!/usr/bin/python2

from __future__ import print_function
import os
import sys
import time
import libgfchangelog

cl = libgfchangelog.Changes()

def get_changes(brick, scratch_dir, log_file, log_level, interval):
    change_list = []
    try:
        cl.cl_init()
        cl.cl_register(brick, scratch_dir, log_file, log_level)
        while True:
            cl.cl_scan()
            change_list = cl.cl_getchanges()
            if change_list:
                print(change_list)
            for change in change_list:
                print(('done with %s' % (change)))
                cl.cl_done(change)
            time.sleep(interval)
    except OSError:
        ex = sys.exc_info()[1]
        print(ex)

if __name__ == '__main__':
    if len(sys.argv) != 6:
        print(("usage: %s <brick> <scratch-dir> <log-file> <fetch-interval>"
              % (sys.argv[0])))
        sys.exit(1)
    get_changes(sys.argv[1], sys.argv[2], sys.argv[3], 9, int(sys.argv[4]))
