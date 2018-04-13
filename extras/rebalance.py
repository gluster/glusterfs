#!/usr/bin/python2

from __future__ import print_function
import atexit
import copy
import optparse
import os
import pipes
import shutil
import string
import subprocess
import sys
import tempfile
import volfilter
import platform

# It's just more convenient to have named fields.
class Brick:
        def __init__ (self, path, name):
                self.path = path
                self.sv_name = name
                self.size = 0
                self.curr_size = 0
                self.good_size = 0
        def set_size (self, size):
                self.size = size
        def set_range (self, rs, re):
                self.r_start = rs
                self.r_end = re
                self.curr_size = self.r_end - self.r_start + 1
        def __repr__ (self):
                value = self.path[:]
                value += "(%d," % self.size
                if self.curr_size:
                        value += "0x%x,0x%x)" % (self.r_start, self.r_end)
                else:
                        value += "-)"
                return value

def get_bricks (host, vol):
        t = pipes.Template()
        t.prepend("gluster --remote-host=%s system getspec %s"%(host,vol),".-")
        return t.open(None,"r")

def generate_stanza (vf, all_xlators, cur_subvol):
        sv_list = []
        for sv in cur_subvol.subvols:
                generate_stanza(vf,all_xlators,sv)
                sv_list.append(sv.name)
        vf.write("volume %s\n"%cur_subvol.name)
        vf.write("  type %s\n"%cur_subvol.type)
        for kvpair in cur_subvol.opts.iteritems():
                vf.write("  option %s %s\n"%kvpair)
        if sv_list:
                vf.write("  subvolumes %s\n"%string.join(sv_list))
        vf.write("end-volume\n\n")


def mount_brick (localpath, all_xlators, dht_subvol):

        # Generate a volfile.
        vf_name = localpath + ".vol"
        vf = open(vf_name,"w")
        generate_stanza(vf,all_xlators,dht_subvol)
        vf.flush()
        vf.close()

        # Create a brick directory and mount the brick there.
        os.mkdir(localpath)
        subprocess.call(["glusterfs","-f",vf_name,localpath])

# We use the command-line tools because there's no getxattr support in the
# Python standard library (which is ridiculous IMO).  Adding the xattr package
# from PyPI would create a new and difficult dependency because the bits to
# satisfy it don't seem to exist in Fedora.  We already expect the command-line
# tools to be there, so it's safer just to rely on them.
#
# We might have to revisit this if we get as far as actually issuing millions
# of setxattr requests.  Even then, it might be better to do that part with a C
# program which has only a build-time dependency.
def get_range (brick):
        t = pipes.Template()
        cmd = "getfattr -e hex -n trusted.glusterfs.dht %s 2> /dev/null"
        t.prepend(cmd%brick,".-")
        t.append("grep ^trusted.glusterfs.dht=","--")
        f = t.open(None,"r")
        try:
                value = f.readline().rstrip().split('=')[1][2:]
        except:
                print("could not get layout for %s (might be OK)" % brick)
                return None
        v_start = int("0x"+value[16:24],16)
        v_end = int("0x"+value[24:32],16)
        return (v_start, v_end)

def calc_sizes (bricks, total):
        leftover = 1 << 32
        for b in bricks:
               if b.size:
                        b.good_size = (b.size << 32) / total
                        leftover -= b.good_size
               else:
                        b.good_size = 0
        if leftover:
                # Add the leftover to an old brick if we can.
                for b in bricks:
                        if b.good_size:
                                b.good_size += leftover
                                break
                else:
                        # Fine, just add it wherever.
                        bricks[0].good_size += leftover

# Normalization means sorting the bricks by r_start and (b) ensuring that there
# are no gaps.
def normalize (in_bricks):
        out_bricks = []
        curr_hash = 0
        used = 0
        while curr_hash < (1<<32):
                curr_best = None
                for b in in_bricks:
                        if b.r_start == curr_hash:
                                used += 1
                                out_bricks.append(b)
                                in_bricks.remove(b)
                                curr_hash = b.r_end + 1
                                break
                else:
                        print("gap found at 0x%08x" % curr_hash)
                        sys.exit(1)
        return out_bricks + in_bricks, used

def get_score (bricks):
        score = 0
        curr_hash = 0
        for b in bricks:
                if not b.curr_size:
                        curr_hash += b.good_size
                        continue
                new_start = curr_hash
                curr_hash += b.good_size
                new_end = curr_hash - 1
                if new_start > b.r_start:
                        max_start = new_start
                else:
                        max_start = b.r_start
                if new_end < b.r_end:
                        min_end = new_end
                else:
                        min_end = b.r_end
                if max_start <= min_end:
                        score += (min_end - max_start + 1)
        return score

if __name__ == "__main__":

	my_usage = "%prog [options] server volume [directory]"
	parser = optparse.OptionParser(usage=my_usage)
        parser.add_option("-f", "--free-space", dest="free_space",
                          default=False, action="store_true",
                          help="use free space instead of total space")
        parser.add_option("-l", "--leave-mounted", dest="leave_mounted",
                          default=False, action="store_true",
                          help="leave subvolumes mounted")
        parser.add_option("-v", "--verbose", dest="verbose",
                          default=False, action="store_true",
                          help="verbose output")
	options, args = parser.parse_args()

        if len(args) == 3:
                fix_dir = args[2]
        else:
                if len(args) != 2:
                        parser.print_help()
                        sys.exit(1)
                fix_dir = None
        hostname, volname = args[:2]

        # Make sure stuff gets cleaned up, even if there are exceptions.
        orig_dir = os.getcwd()
        work_dir = tempfile.mkdtemp()
        bricks = []
        def cleanup_workdir ():
                os.chdir(orig_dir)
                if options.verbose:
                        print("Cleaning up %s" % work_dir)
                for b in bricks:
                        subprocess.call(["umount",b.path])
                shutil.rmtree(work_dir)
        if not options.leave_mounted:
                atexit.register(cleanup_workdir)
        os.chdir(work_dir)

        # Mount each brick individually, so we can issue brick-specific calls.
        if options.verbose:
                print("Mounting subvolumes...")
        index = 0
        volfile_pipe = get_bricks(hostname,volname)
        all_xlators, last_xlator = volfilter.load(volfile_pipe)
        for dht_vol in all_xlators.itervalues():
                if dht_vol.type == "cluster/distribute":
                        break
        else:
                print("no DHT volume found")
                sys.exit(1)
        for sv in dht_vol.subvols:
                #print "found subvol %s" % sv.name
                lpath = "%s/brick%s" % (work_dir, index)
                index += 1
                mount_brick(lpath,all_xlators,sv)
                bricks.append(Brick(lpath,sv.name))
        if index == 0:
                print("no bricks")
                sys.exit(1)

        # Collect all of the sizes.
        if options.verbose:
                print("Collecting information...")
        total = 0
        for b in bricks:
                info = os.statvfs(b.path)
                # On FreeBSD f_bsize (info[0]) contains the optimal I/O size,
                # not the block size as it's found on Linux. In this case we
                # use f_frsize (info[1]).
                if platform.system() == 'FreeBSD':
                        bsize = info[1]
                else:
                        bsize = info[0]
                # We want a standard unit even if different bricks use
                # different block sizes.  The size is chosen to avoid overflows
                # for very large bricks with very small block sizes, but also
                # accommodate filesystems which use very large block sizes to
                # cheat on benchmarks.
                blocksper100mb = 104857600 / bsize
                if options.free_space:
                        size = info[3] / blocksper100mb
                else:
                        size = info[2] / blocksper100mb
                if size <= 0:
                        print("brick %s has invalid size %d" % (b.path, size))
                        sys.exit(1)
                b.set_size(size)
                total += size

        # Collect all of the layout information.
        for b in bricks:
                hash_range = get_range(b.path)
                if hash_range is not None:
                        rs, re = hash_range
                        if rs > re:
                                print("%s has backwards hash range" % b.path)
                                sys.exit(1)
                        b.set_range(hash_range[0],hash_range[1])

        if options.verbose:
                print("Calculating new layouts...")
        calc_sizes(bricks,total)
        bricks, used = normalize(bricks)

        # We can't afford O(n!) here, but O(n^2) should be OK and the result
        # should be almost as good.
        while used < len(bricks):
                best_place = used
                best_score = get_score(bricks)
                for i in xrange(used):
                        new_bricks = bricks[:]
                        del new_bricks[used]
                        new_bricks.insert(i,bricks[used])
                        new_score = get_score(new_bricks)
                        if new_score > best_score:
                                best_place = i
                                best_score = new_score
                if best_place != used:
                        nb = bricks[used]
                        del bricks[used]
                        bricks.insert(best_place,nb)
                used += 1

        # Finalize whatever we decided on.
        curr_hash = 0
        for b in bricks:
                b.r_start = curr_hash
                curr_hash += b.good_size
                b.r_end = curr_hash - 1

        print("Here are the xattr values for your size-weighted layout:")
        for b in bricks:
                print("  %s: 0x0000000200000000%08x%08x" % (
                        b.sv_name, b.r_start, b.r_end))

        if fix_dir:
                if options.verbose:
                        print("Fixing layout for %s" % fix_dir)
                for b in bricks:
                        value = "0x0000000200000000%08x%08x" % (
                                b.r_start, b.r_end)
                        path = "%s/%s" % (b.path, fix_dir)
                        cmd = "setfattr -n trusted.glusterfs.dht -v %s %s" % (
                                value, path)
                        print(cmd)

        if options.leave_mounted:
                print("The following subvolumes are still mounted:")
                for b in bricks:
                        print("%s on %s" % (b.sv_name, b.path))
                print("Don't forget to clean up when you're done.")

