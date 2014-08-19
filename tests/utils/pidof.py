#!/usr/bin/env python

import sys

try:
    import psutil
except ImportError:
    print("Please install psutil --> pip install psutil")
    sys.exit(1)

def pmap_find(p, name):
    for m in p.memory_maps(grouped=True):
        if m.path.endswith("%s.so" % name):
            return True
        continue
    return False

def pidof(processname):
    for p in psutil.process_iter():
        if p.pid == 0:
            continue
        if "gluster" in processname:
            if processname == "glusterd" and pmap_find(p, "glusterd"):
                print (p.pid)
            if processname == "glusterfs" and pmap_find(p, "client"):
                print (p.pid)
            if processname == "glusterfsd" and pmap_find(p, "posix-acl"):
                print (p.pid)
            continue
        if processname.strip() == p.name():
            print (p.pid)

def main(argv):
    if len(argv) < 2:
        sys.stderr.write("Usage: %s <processname>\n" % (argv[0],))
        return 1
    try:
        pidof(argv[1])
    except Exception as err:
        print err
        sys.stderr.write("Please be root - %s\n" % err);
        sys.exit(1)

if __name__ == "__main__":
    main(sys.argv)
