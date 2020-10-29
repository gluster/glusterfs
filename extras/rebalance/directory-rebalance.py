#!/usr/bin/python3
# -*- coding: utf-8 -*-
#
#  Copyright (c) 2020 PhonePe. <http://www.phonepe.com>
#  This file is part of GlusterFS.
#
#  This file is licensed to you under your choice of the GNU Lesser
#  General Public License, version 3 or any later version (LGPLv3 or
#  later), or the GNU General Public License, version 2 (GPLv2), in all
#  cases as published by the Free Software Foundation.
#

import argparse
import os
import errno
import time
import sys
import datetime
import hashlib
import logging

def size_fmt(num):
    for unit in ['B','KiB','MiB','GiB','TiB','PiB','EiB','ZiB']:
        if abs(num) < 1024.0:
            return f"{num:7.2f} {unit}"
        num /= 1024.0
    return "f{num:.2f} YiB"

def time_fmt(fr_sec):
    return str(datetime.timedelta(seconds=int(fr_sec)))

def crawl_progress(count, size):
    sys.stdout.write(f'Building index of {count} files with cumulative size {size} to attempt rebalance.\r')
    sys.stdout.flush()

#https://gist.github.com/vladignatyev/06860ec2040cb497f0f3
def progress(count, total, status=''):
    bar_len = 60
    filled_len = int(round(bar_len * count / float(total)))

    percents = round(100.0 * count / float(total), 1)
    bar = '=' * filled_len + '-' * (bar_len - filled_len)

    sys.stdout.write(f'[{bar}] {percents}% ...{status}\r')
    sys.stdout.flush()

def progress_done():
    print()

class Rebalancer:
    def __init__(self, path):
        self.path = path # Path on which migration script is executed
        self.migration_duration = 0 #Time spent in migrating data
        self.skipped_migration_duration = 0 #Time spent in migrating data
        self.migrated_files = 0 #Number of files migrated successfully
        self.total_files = 0 #Total number of files scanned
        self.total_size = 0 #Cumulative size of files scanned so far
        self.expected_total_size = 0 #This is calculated at the time of populating index
        self.expected_total_files = 0 #This is calculated at the time of populating index
        self.migrated_size = 0 #Cumulative size of files migrated
        self.index = self.get_file_name('index')
        self.init_logging()
        self.rebalance_start = 0 #Start time to be updated in run

    def __enter__(self):
        return self

    #Generate a unique name for the given path. format of the path will be
    #rebalance-<hiphenated-path>-<first-8-hex-chars-of-md5-digest>.suffix
    #If the length of this name is > 255 then hiphenated-path is truncated to
    #make space
    def get_file_name(self, suffix):
        name_suffix = hashlib.md5(self.path.encode('utf-8')).hexdigest()[:8]+'.'+suffix
        name_suffix = '-' + name_suffix
        max_name_len = os.pathconf('.', 'PC_NAME_MAX')
        name = 'rebalance'+self.path.replace('/', '-')
        if len(name) > max_name_len - len(name_suffix):
            name = name[:(max_name_len - len(name_suffix))]
        name += name_suffix
        return name

    #Log format is as follows
    #2020-10-21 18:24:27.838 INFO /mnt/glusterfs/0/aaaaaaaaaa/1 - 1.0 KiB [1024] - 74.6 KiB/s
    def init_logging(self):
        logging.basicConfig(filename=self.get_file_name('log'), level=logging.DEBUG,
                            format='%(asctime)s.%(msecs)03d %(levelname)s %(message)s',
                            datefmt='%Y-%m-%d %H:%M:%S')

    #Executes the setxattr syscall to trigger migration
    def migrate_data(self, f):
        size_now = 0
        try:
            size_now = os.stat(f).st_size
            os.setxattr(f, "trusted.distribute.migrate-data", b"1",
                        follow_symlinks=False)
            return True, size_now, None #Indicate that migration happened

        except OSError as e:
            return False, size_now, e

    #Updates the total,migrated,skipped files/size and durations of the migrations
    def migrate_with_stats(self, f, size):
        migration_start = time.perf_counter()
        result, size_now, err = self.migrate_data(f)
        migration_end = time.perf_counter()
        duration = migration_end - migration_start

        size_diff = size_now - size
        if err is not None:
            if err.errno == errno.EEXIST:
                logging.info(f"{f} - Not needed")
            elif err.errno == errno.ENOENT:
                #Account for file deletion
                #File could be deleted just after stat, so update size_diff again
                size_diff = -size
                self.expected_total_files -= 1
                logging.info(f"{f} - file not present anymore")
            else:
                logging.critical(f"{f} - {err} - exiting.")
                raise err

        #Account for size changes between indexing and rebalancing
        self.expected_total_size += size_diff
        size = size_now
        if result:
            if size != 0 and duration != 0.0:
                logging.info(f"{f} - {size_fmt(size)} [{size}] - {size_fmt(size/duration)}/s")
            else:
                logging.info(f"{f} - {size_fmt(size)} [{size}]")

        self.total_files += 1
        self.total_size += size
        if result == True:
            self.migrated_files += 1
            self.migrated_size += size
            self.migration_duration += duration
        else:
            self.skipped_migration_duration += duration

        return result

    def run(self):
        print("Starting Rebalance")
        self.rebalance_start = time.perf_counter()
        with open(self.index) as f:
            sample_size = 1024*1024
            sample_files = 100
            i = 1
            for line in f:
                file_path, _, file_size = line.rpartition('-')
                file_size = int(file_size)
                self.migrate_with_stats(file_path, file_size)
                if self.migrated_size > i*sample_size and self.migrated_files > i*sample_files:
                    i += 1
                    now = time.perf_counter()
                    duration = now - self.rebalance_start
                    if duration != 0.0:
                        speed = self.migrated_size/duration
                        migration_fraction = self.migrated_size/self.total_size
                        eta = ((self.expected_total_size - self.total_size)/speed)*migration_fraction
                        progress(self.total_size, self.expected_total_size, f"ETA: {time_fmt(eta)}")

    #For each file in the directory recursively, writes <file-path>-<size> to index file
    def generate_rebalance_file_index(self):
        with open(self.index, 'w') as file_index:
            total_size = 0
            for d, _, files in os.walk(self.path):
                for f in files:
                    try:
                        path = os.path.join(d, f)
                        size = os.stat(path).st_size
                        file_index.write(path+'-'+str(size)+'\n')
                        self.expected_total_size += size
                        self.expected_total_files += 1
                        crawl_progress(self.expected_total_files, size_fmt(self.expected_total_size))
                    except OSError as err:
                        progress_done()
                        print(f"OS error: {err}")
        progress_done()

    #Stops the progress printing and prints stats collected so far
    def __exit__(self, exc_type, exc_value, traceback):
        progress_done()
        if self.rebalance_start != 0:
            rebalance_end = time.perf_counter()
            self.duration = rebalance_end - self.rebalance_start

            if self.total_files != 0:
                print(f"Migrated {self.migrated_files} / {self.total_files} files [{self.migrated_files/self.total_files:.2%}]")
                if self.total_size != 0:
                    print(f"Migrated {size_fmt(self.migrated_size)} / {size_fmt(self.total_size)} data [{self.migrated_size/self.total_size:.2%}]")
            print(f"Run time: {time_fmt(self.duration)}")
            print(f"Time spent in migration: {time_fmt(self.migration_duration)} [{self.migration_duration/self.duration:.2%}]")
            print(f"Time spent in skipping: {time_fmt(self.skipped_migration_duration)} [{self.skipped_migration_duration/self.duration:.2%}]")

#/proc/mounts has active mount information. It checks that the given path is
#mounted on glusterfs
def check_glusterfs_supported_path(p):
    real_path = os.path.realpath(p)
    if not os.path.isdir(real_path):
        raise argparse.ArgumentTypeError(f"{real_path} is not a valid directory")

    glusterfs_mounts = {}
    with open("/proc/mounts") as f:
        for line in f:
            words = line.split()
            if len(words) < 3:
                continue
            if words[2] == 'fuse.glusterfs':
                glusterfs_mounts[words[1]] = 1

    p = real_path
    while p != '':
        if p in glusterfs_mounts:
            return real_path
        elif os.path.ismount(p):
            raise argparse.ArgumentTypeError(f"{real_path} is not a valid glusterfs path")
        else:
            p, _, _ = p.rpartition('/')

    raise argparse.ArgumentTypeError(f"{real_path} is not a valid glusterfs path")

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument("path", type=check_glusterfs_supported_path)
    args = parser.parse_args()
    with Rebalancer(args.path) as r:
        r.generate_rebalance_file_index()
        r.run()
