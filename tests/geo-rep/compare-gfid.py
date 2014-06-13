#!/usr/bin/python

# Most of this script was written by M S Vishwanath Bhat (vbhat@redhat.com)

import re
import os
import sys
import xattr
import tempfile


def parse_url(url):
    match = re.search(r'([\w - _ @ \.]+)::([\w - _ @ \.]+)', url)
    if match:
        node = match.group(1)
        vol = match.group(2)
    else:
        print 'given url is not a valid url.'
        sys.exit(1)
    return node, vol


def cleanup(master_mnt, slave_mnt):
    try:
        os.system("umount %s" % (master_mnt))
    except:
        print("Failed to unmount the master volume")
    try:
        os.system("umount %s" % (slave_mnt))
    except:
        print("Failed to unmount the slave volume")

    os.removedirs(master_mnt)
    os.removedirs(slave_mnt)


def main():

    masterurl = sys.argv[1]
    slaveurl = sys.argv[2]
    slave_node, slavevol = parse_url(slaveurl)
    master_node, mastervol = parse_url(masterurl)

    master_mnt = tempfile.mkdtemp()
    slave_mnt = tempfile.mkdtemp()

    try:
        print "Mounting master volume on a temp mnt_pnt"
        os.system("glusterfs -s %s --volfile-id %s %s" % (master_node,
                                                          mastervol,
                                                          master_mnt))
    except:
        print("Failed to mount the master volume")
        cleanup(master_mnt, slave_mnt)
        sys.exit(1)

    try:
        print "Mounting slave voluem on a temp mnt_pnt"
        os.system("glusterfs -s %s --volfile-id %s %s" % (slave_node, slavevol,
                                                          slave_mnt))
    except:
        print("Failed to mount the master volume")
        cleanup(master_mnt, slave_mnt)
        sys.exit(1)

    slave_file_list = [slave_mnt]
    for top, dirs, files in os.walk(slave_mnt, topdown=False):
        for subdir in dirs:
            slave_file_list.append(os.path.join(top, subdir))
        for file in files:
            slave_file_list.append(os.path.join(top, file))

    # chdir and then get the gfid, so that you don't need to replace
    gfid_attr = 'glusterfs.gfid'
    ret = 0
    for sfile in slave_file_list:
        mfile = sfile.replace(slave_mnt, master_mnt)
        if xattr.getxattr(sfile, gfid_attr, True) != xattr.getxattr(
                mfile, gfid_attr, True):
            print ("gfid of file %s in slave is different from %s" +
                   " in master" % (sfile, mfile))
            ret = 1

    cleanup(master_mnt, slave_mnt)

    sys.exit(ret)


if __name__ == '__main__':
    if len(sys.argv[1:]) < 2:
        print ("Please pass master volume name and slave url as arguments")
        print ("USAGE : python <script> <master-host>::<master-vol> " +
               "<slave-host>::<slave-vol>")
        sys.exit(1)
    main()
