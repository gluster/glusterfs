#!/usr/bin/python

import sys
import os
import re
import tempfile
import subprocess
from multiprocessing import Pool
import time
from optparse import OptionParser

slave_dict = {}
master_res = ''


def get_arequal_checksum(me, mnt):
    global slave_dict
    master_cmd = ['./tests/utils/arequal-checksum', '-p', mnt]
    print "Calculating  "+me+" checksum ..."
    print ""
    p = subprocess.Popen(master_cmd, stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE)
    ret = p.wait()
    stdout, stderr = p.communicate()
    if ret:
        print "Failed to get the checksum of " + me + " with following error"
        print stderr
        return 1
    else:
        return stdout


def get_file_count(me, mnt):
    global slave_dict
    master_cmd = ['find ' + mnt + ' |wc -l']
    print "Calculating  " + me + " files ..."
    print ""
    p = subprocess.Popen(master_cmd, stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE, shell=True)
    ret = p.wait()
    stdout, stderr = p.communicate()
    if ret:
        print "Failed to get the count of files in " + me
        + " with following error"
        print stderr
        return 1
    else:
        return stdout.strip()


def compare_checksum(master_mnt, slave_dict):
    proc = len(slave_dict)+1
    pool = Pool(processes=proc)
    master_res = pool.apply_async(get_arequal_checksum, args=("master",
                                                              master_mnt))
    results = [(slave, pool.apply_async(get_arequal_checksum,
                                        args=(slave_dict[slave]["vol"],
                                              slave_dict[slave]["mnt"])))
               for slave in slave_dict]

    pool.close()
    pool.join()
    for slave, result in results:
        slave_dict[slave]["res"] = result.get()
        # exception:  OSError

    master_res = master_res.get()

    print "arequal-checksum of master is : \n %s" % master_res
    for slave in slave_dict:
        print "arequal-checksum of geo_rep_slave %s: \n %s" % (
            slave_dict[slave]["vol"], slave_dict[slave]["res"])

    master_files, master_total = re.findall('Total[\s]+:\s(\w+)', master_res)
    master_reg_meta, master_reg = re.findall('Regular files[\s]+:\s(\w+)',
                                             master_res)[1:]
    master_dir_meta, master_dir = re.findall('Directories[\s]+:\s(\w+)',
                                             master_res)[1:]

    ret = 0
    for slave in slave_dict:
        slave_dict[slave]["files"], slave_dict[slave]["total"] = re.findall(
            'Total[\s]+:\s(\w+)', slave_dict[slave]["res"])
        slave_dict[slave]["reg_meta"], slave_dict[slave]["reg"] = re.findall(
            'Regular files[\s]+:\s(\w+)', slave_dict[slave]["res"])[1:]
        slave_dict[slave]["dir_meta"], slave_dict[slave]["dir"] = re.findall(
            'Directories[\s]+:\s(\w+)', slave_dict[slave]["res"])[1:]

        if master_reg_meta != slave_dict[slave]["reg_meta"]:
            print ("Meta data checksum for regular files doesn't match " +
                   "between master and  "+slave_dict[slave]["vol"])
            ret = 67

        if master_dir_meta != slave_dict[slave]["dir_meta"]:
            print ("Meta data checksum for directories doesn't match " +
                   "between master and "+slave_dict[slave]["vol"])
            ret = 68

        if master_files != slave_dict[slave]["files"]:
            print ("Failed to sync all the files from master to " +
                   slave_dict[slave]["vol"])
            ret = 1

        if master_total != slave_dict[slave]["total"]:
            if master_reg != slave_dict[slave]["reg"]:
                print ("Checksum for regular files doesn't match " +
                       "between master and "+slave_dict[slave]["vol"])
                ret = 1
            elif master_dir != slave_dict[slave]["dir"]:
                print ("Checksum for directories doesn't match between " +
                       "master and "+slave_dict[slave]["vol"])
                ret = 1
            else:
                print ("Checksum for symlinks or others doesn't match " +
                       "between master and "+slave_dict[slave]["vol"])
                ret = 1

        if ret is 0:
            print ("Successfully synced all the files from master " +
                   "to the "+slave_dict[slave]["vol"])

    return ret


def compare_filecount(master_mnt, slave_dict):
    proc = len(slave_dict)+1
    pool = Pool(processes=proc)

    master_res = pool.apply_async(get_file_count, args=("master", master_mnt))
    results = [(slave, pool.apply_async(get_file_count,
                                        args=(slave_dict[slave]["vol"],
                                              slave_dict[slave]["mnt"])))
               for slave in slave_dict]

    pool.close()
    pool.join()
    for slave, result in results:
        slave_dict[slave]["res"] = result.get()

    master_res = master_res.get()
    ret = 0
    for slave in slave_dict:
        if not master_res == slave_dict[slave]["res"]:
            print ("files count between master and " +
                   slave_dict[slave]["vol"]+" doesn't match yet")
            ret = 1

    return ret


def parse_url(url):
    match = re.search(r'([\w - _ @ \.]+)::([\w - _ @ \.]+)', url)
    if match:
        node = match.group(1)
        vol = match.group(2)
    else:
        print 'given url is not a valid.'
        sys.exit(1)
    return node, vol


def cleanup(master_mnt, slave_dict):
    try:
        os.system("umount %s" % (master_mnt))
    except:
        print("Failed to unmount the master volume")

    for slave in slave_dict:

        try:
            os.system("umount %s" % (slave_dict[slave]["mnt"]))
            os.removedirs(slave_dict[slave]["mnt"])
        except:
            print("Failed to unmount the "+slave+" volume")

    os.removedirs(master_mnt)


def main():

    slaves = args[1:]

    masterurl = args[0]
    master_node, mastervol = parse_url(masterurl)
    master_mnt = tempfile.mkdtemp()

    i = 1
    for slave in slaves:
        slave_dict["slave"+str(i)] = {}
        slave_dict["slave"+str(i)]["node"], slave_dict[
            "slave"+str(i)]["vol"] = parse_url(slave)
        slave_dict["slave"+str(i)]["mnt"] = tempfile.mkdtemp()
        i += 1

    try:
        print ("mounting the master volume on "+master_mnt)
        os.system("glusterfs -s  %s --volfile-id %s %s" % (master_node,
                                                           mastervol,
                                                           master_mnt))
        time.sleep(3)
    except:
        print("Failed to mount the master volume")

    for slave in slave_dict:
        print slave
        print slave_dict[slave]
        try:
            print ("mounting the slave volume on "+slave_dict[slave]['mnt'])
            os.system("glusterfs -s %s --volfile-id %s %s" % (
                slave_dict[slave]["node"], slave_dict[slave]["vol"],
                slave_dict[slave]["mnt"]))
            time.sleep(3)
        except:
            print("Failed to mount the "+slave+" volume")

    res = 0
    if option.check == "arequal":
        res = compare_checksum(master_mnt, slave_dict)
    elif option.check == "find":
        res = compare_filecount(master_mnt, slave_dict)
    else:
        print "wrong options given"

    cleanup(master_mnt, slave_dict)

    sys.exit(res)


if __name__ == '__main__':

    usage = "usage: %prog [option] <master-host>::<master-vol> \
    <slave1-host>::<slave1-vol> . . ."
    parser = OptionParser(usage=usage)
    parser.add_option("-c", dest="check", action="store", type="string",
                      default="arequal",
                      help="size of the files to be used [default: %default]")
    (option, args) = parser.parse_args()
    if not args:
        print "usage: <script> [option] <master-host>::<master-vol>\
         <slave1-host>::<slave1-vol> . . ."
        print ""
        sys.exit(1)

    main()
