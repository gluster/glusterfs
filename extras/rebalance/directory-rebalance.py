#!/usr/bin/python3
import argparse
import os
import errno
import time
import sys
import datetime
import hashlib
import logging

def size_fmt(num, suffix='B'):
    for unit in ['','Ki','Mi','Gi','Ti','Pi','Ei','Zi']:
        if abs(num) < 1024.0:
            return "{:3.1f} {}{}".format(num, unit, suffix)
        num /= 1024.0
    return "{:.2f} {}{}".format(num, 'Yi', suffix)

def time_fmt(fr_sec):
    return str(datetime.timedelta(microseconds=fr_sec * 1000000))

def crawl_progress(count, size):
    sys.stdout.write('Building index of {} files with cumulative size {} to rebalance.\r'.format(count, size))
    sys.stdout.flush()

#https://gist.github.com/vladignatyev/06860ec2040cb497f0f3
def progress(count, total, status=''):
    bar_len = 60
    filled_len = int(round(bar_len * count / float(total)))

    percents = round(100.0 * count / float(total), 1)
    bar = '=' * filled_len + '-' * (bar_len - filled_len)

    sys.stdout.write('[{}] {}% ...{}\r'.format(bar, percents, status))
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
        try:
            os.setxattr(f, "trusted.distribute.migrate-data", b"1",
                        follow_symlinks=False)
            return True, None #Indicate that migration happened

        except OSError as e:
            return False, e

    #Updates the total,migrated,skipped files/size and durations of the migrations
    def migrate_with_stats(self, f, size):
        migration_start = time.perf_counter()
        result, err = self.migrate_data(f)
        migration_end = time.perf_counter()
        duration = migration_end - migration_start

        if err is not None:
            if err.errno == errno.EEXIST:
                logging.info("{} - Not needed".format(f))
            elif err.errno == errno.ENOENT:
                logging.info("{} - file not present anymore".format(f))
            else:
                logging.critical("{} - {} - exiting.".format(f, err))
                self.exit()
                sys.exit(1)

        if result and duration != 0:
            logging.info("{} - {} [{}] - {}/s".format(f, size_fmt(size), size, size_fmt(size/duration)))

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
                        progress(self.total_size, self.expected_total_size, "ETA: {}".format(time_fmt(eta)))

        self.exit()

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
                        print("OS error: {}".format(err))
        progress_done()

    #Stops the progress printing and prints stats collected so far
    def exit(self):
        progress_done()
        rebalance_end = time.perf_counter()
        self.duration = rebalance_end - self.rebalance_start

        if self.total_files != 0:
            print("Migrated {} / {} files [{:.2%}]".format(self.migrated_files, self.total_files, self.migrated_files/self.total_files))
            if self.total_size != 0:
                print("Migrated {} / {} data [{:.2%}]".format(size_fmt(self.migrated_size), size_fmt(self.total_size), self.migrated_size/self.total_size))
        print("Run time: {}".format(time_fmt(self.duration)))
        print("Time spent in migration: {} [{:.2%}]".format(time_fmt(self.migration_duration), self.migration_duration/self.duration))
        print("Time spent in skipping: {} [{:.2%}]".format(time_fmt(self.skipped_migration_duration), self.skipped_migration_duration/self.duration))

#/proc/mounts has active mount information. It checks that the given path is
#mounted on glusterfs
def check_glusterfs_supported_path(p):
    abs_path = os.path.abspath(p)
    if os.path.isdir(abs_path) is False:
        raise argparse.ArgumentTypeError("{} is not a valid directory".format(abs_path))

    glusterfs_mounts = {}
    with open("/proc/mounts") as f:
        for line in f:
            words = line.split()
            if len(words) < 3:
                continue
            if words[2] == 'fuse.glusterfs':
                glusterfs_mounts[words[1]] = 1

    p = abs_path
    while p != '':
        if p in glusterfs_mounts:
            return abs_path
        elif os.path.ismount(p):
            raise argparse.ArgumentTypeError("{} is not a valid glusterfs path".format(abs_path))
        else:
            p, _, _ = p.rpartition('/')

    raise argparse.ArgumentTypeError("{} is not a valid glusterfs path".format(abs_path))

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument("path", type=check_glusterfs_supported_path)
    args = parser.parse_args()
    r = Rebalancer(args.path)
    r.generate_rebalance_file_index()
    r.run()
