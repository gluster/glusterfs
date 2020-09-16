#!/usr/bin/python3
# The following script enables, Detecting, Reporting and Fixing
# anomalies in quota accounting. Run this script with -h option
# for further details.

'''
   Copyright (c) 2018 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
'''
from __future__ import print_function
import os, sys, re
from stat import *
import subprocess
import argparse
import xattr

aggr_size = {}
verbose_mode = False
mnt_path = None
brick_path = None
obj_fix_count = 0
file_count = 0
dir_count = 0

#CONSTANTS
KB = 1024
MB = 1048576
GB = 1048576 * 1024
TB = 1048576 * 1048576

QUOTA_VERBOSE = 0
QUOTA_META_ABSENT = 1
QUOTA_SIZE_MISMATCH = 2

IS_DIRTY ='0x3100'
IS_CLEAN ='0x3000'


epilog_msg='''
            The script attempts to find any gluster accounting issues in the
            filesystem at the given subtree. The script crawls the given
            subdirectory tree doing a stat for all files and compares the
            size reported by gluster quota with the size reported by stat
            calls. Any mismatch is reported. In addition integrity of marker
            xattrs are verified.
            '''

def print_msg(log_type, path, xattr_dict = {}, stbuf = "", dir_size = None):
    if log_type == QUOTA_VERBOSE:
        print('%-24s %-60s\nxattr_values: %s\n%s\n' % ("Verbose", path, xattr_dict, stbuf))
    elif log_type == QUOTA_META_ABSENT:
        print('%-24s %-60s\n%s\n' % ("Quota-Meta Absent", path, xattr_dict))
    elif log_type == QUOTA_SIZE_MISMATCH:
        print("mismatch")
        if dir_size is not None:
            print('%24s %60s %12s %12s' % ("Size Mismatch", path,
                xattr_dict, dir_size))
        else:
            print('%-24s %-60s %-12s %-12s' % ("Size Mismatch", path, xattr_dict,
                   stbuf.st_size))

def size_differs_lot(s1, s2):
    '''
    There could be minor accounting differences between the stat based
    accounting and gluster accounting. To avoid these from throwing lot
    of false positives in our logs. using a threshold of 1M for now.
    TODO: For a deeply nested directory, at higher levels in hierarchy
    differences may not be significant, hence this check needs to be improved.
    '''
    if abs(s1-s2) > 0:
        return True
    else:
        return False

def fix_hardlink_accounting(curr_dict, accounted_dict, curr_size):
    '''
            Hard links are messy.. we have to account them for their parent
            directory. But, stop accounting at the most common ancestor.
            Eg:
                say we have 3 hardlinks : /d1/d2/h1, /d1/d3/h2 and /d1/h3

            suppose we encounter the hard links h1 first , then h2 and then h3.
            while accounting for h1, we account the size until root(d2->d1->/)
            while accounting for h2, we need to account only till d3. (as d1
            and / are accounted for this inode).
            while accounting for h3 we should not account at all.. as all
            its ancestors are already accounted for same inode.

            curr_dict                : dict of hardlinks that were seen and
                                       accounted by the current iteration.
            accounted_dict           : dict of hardlinks that has already been
                                       accounted for.

            size                     : size of the object as accounted by the
                                       curr_iteration.

            Return vale:
            curr_size                : size reduced by hardlink sizes for those
                                       hardlinks that has already been accounted
                                       in current subtree.
            Also delete the duplicate link from curr_dict.
    '''

    dual_accounted_links = set(curr_dict.keys()) & set(accounted_dict.keys())
    for link in dual_accounted_links:
        curr_size = curr_size - curr_dict[link]
        del curr_dict[link]
    return curr_size


def fix_xattr(file_name, mark_dirty):
    global obj_fix_count
    global mnt_path

    if mnt_path is None:
        return
    if mark_dirty:
        print("MARKING DIRTY: " + file_name)
        out = subprocess.check_output (["/usr/bin/setfattr", "-n",
                                       "trusted.glusterfs.quota.dirty",
                                       "-v", IS_DIRTY, file_name])
    rel_path = os.path.relpath(file_name, brick_path)
    print("stat on "  + mnt_path + "/" + rel_path)
    stbuf = os.lstat(mnt_path + "/" + rel_path)

    obj_fix_count += 1

def get_quota_xattr_brick(dpath):
    out = subprocess.check_output (["/usr/bin/getfattr", "--no-dereference",
                                    "-d", "-m.", "-e", "hex", dpath])
    pairs = out.splitlines()

    '''
    Sample output to be parsed:
    [root@dhcp35-100 mnt]# getfattr -d -m. -e hex /export/b1/B0/d14/d13/
    # file: export/b1/B0/d14/d13/
    security.selinux=0x756e636f6e66696e65645f753a6f626a6563745f723a7573725f743a733000
    trusted.gfid=0xbae5e0d2d05043de9fd851d91ecf63e8
    trusted.glusterfs.dht=0x000000010000000000000000ffffffff
    trusted.glusterfs.dht.mds=0x00000000
    trusted.glusterfs.quota.6a7675a3-b85a-40c5-830b-de9229d702ce.contri.39=0x00000000000000000000000000000000000000000000000e
    trusted.glusterfs.quota.dirty=0x3000
    trusted.glusterfs.quota.size.39=0x00000000000000000000000000000000000000000000000e
    '''

    '''
    xattr_dict dictionary holds quota related xattrs
    eg:
    '''

    xattr_dict = {}
    xattr_dict['parents'] = {}

    for xattr in pairs[1:]:
        xattr = xattr.decode("utf-8")
        xattr_key = xattr.split("=")[0]
        if xattr_key == "":
            # skip any empty lines
            continue
        elif not re.search("quota", xattr_key):
            # skip all non quota xattr.
            continue

        xattr_value = xattr.split("=")[1]
        if re.search("contri", xattr_key):

            xattr_version = xattr_key.split(".")[5]
            if 'version' not in xattr_dict:
                xattr_dict['version'] = xattr_version
            else:
                if xattr_version != xattr_dict['version']:
                   print("Multiple xattr version found")


            cur_parent = xattr_key.split(".")[3]
            if cur_parent not in xattr_dict['parents']:
                xattr_dict['parents'][cur_parent] = {}

            contri_dict = xattr_dict['parents'][cur_parent]
            if len(xattr_value) == 34:
                # 34 bytes implies file contri xattr
                # contri format =0x< 16bytes file size><16bytes file count>
                # size is obtained in iatt, file count = 1, dir count=0
                contri_dict['contri_size'] = int(xattr_value[2:18], 16)
                contri_dict['contri_file_count'] = int(xattr_value[18:34], 16)
                contri_dict['contri_dir_count'] = 0
            else:
                # This is a directory contri.
                contri_dict['contri_size'] = int(xattr_value[2:18], 16)
                contri_dict['contri_file_count'] = int(xattr_value[18:34], 16)
                contri_dict['contri_dir_count'] = int(xattr_value[34:], 16)

        elif re.search("size", xattr_key):
            xattr_dict['size'] = int(xattr_value[2:18], 16)
            xattr_dict['file_count'] = int(xattr_value[18:34], 16)
            xattr_dict['dir_count'] = int(xattr_value[34:], 16)
        elif re.search("dirty", xattr_key):
            if xattr_value == IS_CLEAN:
                xattr_dict['dirty'] = False
            elif xattr_value == IS_DIRTY:
                xattr_dict['dirty'] = True
        elif re.search("limit_objects", xattr_key):
            xattr_dict['limit_objects'] = int(xattr_value[2:18], 16)
        elif re.search("limit_set", xattr_key):
            xattr_dict['limit_set'] = int(xattr_value[2:18], 16)

    return xattr_dict

def verify_file_xattr(path, stbuf = None):

    global file_count
    file_count += 1

    if stbuf is None:
        stbuf = os.lstat(path)

    xattr_dict = get_quota_xattr_brick(path)

    for parent in xattr_dict['parents']:
        contri_dict = xattr_dict['parents'][parent]

        if 'contri_size' not in contri_dict or \
           'contri_file_count' not in contri_dict or \
           'contri_dir_count' not in contri_dict:
            print_msg(QUOTA_META_ABSENT, path, xattr_dict, stbuf)
            fix_xattr(path, False)
            return
        elif size_differs_lot(contri_dict['contri_size'], stbuf.st_size):
            print_msg(QUOTA_SIZE_MISMATCH, path, xattr_dict, stbuf)
            fix_xattr(path, False)
            return

    if verbose_mode is True:
        print_msg(QUOTA_VERBOSE, path, xattr_dict, stbuf)


def verify_dir_xattr(path, dir_size):

    global dir_count
    dir_count += 1
    xattr_dict = get_quota_xattr_brick(path)

    stbuf = os.lstat(path)

    for parent in xattr_dict['parents']:
        contri_dict = xattr_dict['parents'][parent]

        if 'size' not in xattr_dict or 'contri_size' not in contri_dict:
            print_msg(QUOTA_META_ABSENT, path)
            fix_xattr(path, True)
            return
        elif size_differs_lot(dir_size, xattr_dict['size']) or \
             size_differs_lot(contri_dict['contri_size'], xattr_dict['size']):
            print_msg(QUOTA_SIZE_MISMATCH, path, xattr_dict, stbuf, dir_size)
            fix_xattr(path, True)
            return

    if verbose_mode is True:
        print_msg("VERBOSE", path, xattr_dict, stbuf, dir_size)


def walktree(t_dir, hard_link_dict):
    '''recursively descend the directory tree rooted at dir,
       aggregating the size
       t_dir            : directory to walk over.
       hard_link_dict   : dict of inodes with multiple hard_links under t_dir
    '''
    global aggr_size
    aggr_size[t_dir] = 0

    for entry in os.listdir(t_dir):
        pathname = os.path.join(t_dir, entry)
        stbuf = os.lstat(pathname)
        if S_ISDIR(stbuf.st_mode):
            # It's a directory, recurse into it
            if entry == '.glusterfs':
                print("skipping " + pathname)
                continue
            descendent_hardlinks = {}
            subtree_size = walktree(pathname, descendent_hardlinks)

            subtree_size = fix_hardlink_accounting(descendent_hardlinks,
                                                   hard_link_dict,
                                                   subtree_size)

            aggr_size[t_dir] = aggr_size[t_dir] + subtree_size

        elif S_ISREG(stbuf.st_mode) or S_ISLNK(stbuf.st_mode):
            # Even a symbolic link file may have multiple hardlinks.

            file_size = stbuf.st_size
            if stbuf.st_nlink > 2:
                # send a single element dict to check if file is accounted.
                file_size = fix_hardlink_accounting({stbuf.st_ino:stbuf.st_size},
                                                    hard_link_dict,
                                                    stbuf.st_size)

                if file_size == 0:
                    print_msg("HARD_LINK (skipped)", pathname, "",
                                stbuf)
                else:
                    print_msg("HARD_LINK (accounted)", pathname, "",
                               stbuf)
                    hard_link_dict[stbuf.st_ino] = stbuf.st_size

            if t_dir in aggr_size:
                aggr_size[t_dir] = aggr_size[t_dir] + file_size
            else:
                aggr_size[t_dir] = file_size
            verify_file_xattr(pathname, stbuf)

        else:
            # Unknown file type, print a message
            print('Skipping %s, due to file mode' % (pathname))

    if t_dir not in aggr_size:
        aggr_size[t_dir] = 0

    verify_dir_xattr(t_dir, aggr_size[t_dir])
    # du also accounts for t_directory sizes
    # aggr_size[t_dir] += 4096

    #cleanup
    ret = aggr_size[t_dir]
    del aggr_size[t_dir]
    return ret


if __name__ == '__main__':

    parser = argparse.ArgumentParser(description='Diagnose quota accounting issues.', epilog=epilog_msg)
    parser.add_argument('brick_path', nargs=1,
                        help='The brick path (or any descendent sub-directory of brick path)',
                        )
    parser.add_argument('--full-logs', dest='verbose', action='store_true',
                   help='''
                         log all the xattr values and stat values reported
                         for analysis. [CAUTION: This can give lot of output
                         depending on FS depth. So one has to make sure enough
                         disk space exists if redirecting to file]
                        '''
                        )
    parser.add_argument('--fix-issues', metavar='mount_path', dest='mnt', action='store',
                   help='''
                         fix accounting issues where the xattr values disagree
                         with stat sizes reported by gluster. A mount is also
                         required for this option to be used.
                         [CAUTION: This will directly modify backend xattr]
                        '''
                        )
    parser.add_argument('--sub-dir', metavar='sub_dir', dest='sub_dir', action='store',
                   help='''
                         limit the crawling and accounting verification/correction
                         to a specific subdirectory.
                        '''
                        )

    args = parser.parse_args()
    verbose_mode = args.verbose
    brick_path = args.brick_path[0]
    sub_dir = args.sub_dir
    mnt_path = args.mnt
    hard_link_dict = {}
    if sub_dir is not None:
        walktree(os.path.join(brick_path, sub_dir), hard_link_dict)
    else:
        walktree(brick_path, hard_link_dict)

    print("Files verified : " + str(file_count))
    print("Directories verified : " + str(dir_count))
    if mnt_path is not None:
        print("Objects Fixed : " + str(obj_fix_count))
