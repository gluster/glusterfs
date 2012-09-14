#!/usr/bin/python

# This script was developed by Vijaykumar Koppad (vkoppad@redhat.com)
# The latest version of this script can found at
# http://github.com/vijaykumar-koppad/crefi

from __future__ import with_statement
import sys
import os
import re
import random
from optparse import OptionParser
import time
import string
import errno

def os_rd(src, size):
    fd = os.open(src,os.O_RDONLY)
    data = os.read(fd, size)
    os.close(fd)
    return data

def os_wr(dest, data):
    fd = os.open(dest,os.O_WRONLY|os.O_CREAT|os.O_EXCL, 0644)
    os.write(fd, data)
    os.close(fd)
    return

def create_sparse_file(fil):
    if option.size:
        option.random = False
        size = option.size
    else:
        size = random.randint(option.min, option.max)
    data = os_rd("/dev/zero", size)
    os_wr(fil, data)
    return

def create_binary_file(fil):
    if option.size:
        option.random = False
        size = option.size
    else:
        size = random.randint(option.min, option.max)
    data = os_rd("/dev/urandom", size)
    os_wr(fil, data)
    return

def create_txt_file(fil):
    if option.size:
        option.random = False
        size = option.size
    else:
        size = random.randint(option.min, option.max)
    if size < 500*1024:
        data = os_rd("/etc/services", size)
        os_wr(fil, data)
    else:
        data = os_rd("/etc/services", 500*1024)
        file_size = 0
        fd = os.open(fil,os.O_WRONLY|os.O_CREAT|os.O_EXCL, 0644)
        while file_size < size:
            os.write(fd, data)
            file_size += 500*1024
        os.close(fd)
    return

def get_filename():
    size = option.flen
    char = string.uppercase+string.digits
    st = ''.join(random.choice(char) for i in range(size))
    ti = str((hex(int(str(time.time()).split('.')[0])))[2:])
    return ti+"~~"+st

def text_files(files, file_count):
    for k in range(files):
        if not file_count%option.inter:
            print file_count
        fil = get_filename()
        create_txt_file(fil)
        file_count += 1
    return file_count

def sparse_files(files, file_count):
    for k in range(files):
        if not file_count%option.inter:
            print file_count
        fil = get_filename()
        create_sparse_file(fil)
        file_count += 1
    return file_count

def binary_files(files, file_count):
    for k in range(files):
        if not file_count%option.inter:
            print file_count
        fil = get_filename()
        create_binary_file(fil)
        file_count += 1
    return file_count

def human2bytes(size):
    size_short = {
        1024 : ['K','KB','KiB','k','kB','kiB'],
        1024*1024 : ['M','MB','MiB'],
        1024*1024*1024 : ['G','GB','GiB']
}
    num = re.search('(\d+)',size).group()
    ext = size[len(num):]
    num = int(num)
    if ext == '':
        return num
    for value, keys in size_short.items():
        if ext in keys:
            size = num*value
            return size

def multipledir(mnt_pnt,brdth,depth,files):
    files_count = 1
    for i in range(brdth):
        breadth = mnt_pnt+"/"+str(i)
        try:
           os.makedirs(breadth)
        except OSError as ex:
            if not ex.errno is errno.EEXIST:
                raise
        os.chdir(breadth)
        dir_depth = breadth
        print breadth
        for j in range(depth):
            dir_depth = dir_depth+"/"+str(j)
            try:
                os.makedirs(dir_depth)
            except OSError as ex:
                if not ex.errno is errno.EEXIST:
                    raise
            os.chdir(dir_depth)
            if option.file_type == "text":
                files_count = text_files(files, files_count)
            elif option.file_type == "sparse":
                files_count = sparse_files(files, files_count)
            elif option.file_type == "binary":
                files_count = binary_files(files, files_count)
            else:
                print "Not a valid file type"
                sys.exit(1)

def singledir(mnt_pnt, files):
    files_count = 1
    os.chdir(mnt_pnt)
    if option.file_type == "text":
        files_count = text_files(files, files_count)
    elif option.file_type == "sparse":
        files_count = sparse_files(files, files_count)
    elif option.file_type == "binary":
        files_count = binary_files(files, files_count)
    else:
        print "Not a valid file type"
        sys.exit(1)

if __name__ == '__main__':
    usage = "usage: %prog [option] <MNT_PT>"
    parser = OptionParser(usage=usage)
    parser.add_option("-n", dest="files",type="int" ,default=100,
                      help="number of files in each level [default: %default]")
    parser.add_option("--size", action = "store",type="string",
                      help="size of the files to be used")
    parser.add_option("--random",  action="store_true", default=True,
                      help="random size of the file between --min and --max "
                      "[default: %default]")
    parser.add_option("--max", action = "store",type="string", default="500K",
                      help="maximum size of the files, if random is True "
                      "[default: %default]")
    parser.add_option("--min", action = "store",type="string", default="10K",
                      help="minimum size of the files, if random is True "
                      "[default: %default]" )
    parser.add_option("--single", action="store_true", dest="dir",default=True,
                      help="create files in single directory [default: %default]" )
    parser.add_option("--multi", action="store_false", dest="dir",
                      help="create files in multiple directories")
    parser.add_option("-b", dest="brdth",type="int",default=5,
                      help="number of directories in one level(works with --multi)[default: %default]")
    parser.add_option("-d", dest="depth",type="int",default=5,
                      help="number of levels of directories(works with --multi)[default: %default]")
    parser.add_option("-l", dest="flen",type="int" ,default=10,
                      help="number of bytes for filename "
                      "[default: %default]")
    parser.add_option("-t","--type", action="store", type="string" , dest="file_type",default="text",
                      help="type of the file to be created (text, sparse, binary) [default: %default]" )
    parser.add_option("-I", dest="inter", type="int", default=100,
                      help="print number files created of interval [defailt: %dafault]")
    (option,args) = parser.parse_args()
    if not args:
        print "usage: <script> [option] <MNT_PT>"
        print ""
        sys.exit(1)
    args[0] = os.path.abspath(args[0])
    if option.size:
        option.size = human2bytes(option.size)
    else:
        option.max = human2bytes(option.max)
        option.min = human2bytes(option.min)
    if option.dir:
        singledir(args[0], option.files)
    else:
        multipledir(args[0], option.brdth, option.depth, option.files)
    print "creation of files completed.\n"
