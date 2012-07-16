import ctypes
import os
import sys

# Looks like ctypes is having trouble with dependencies, so just force them to
# load with RTLD_GLOBAL until I figure that out.
glfs = ctypes.CDLL("libglusterfs.so",ctypes.RTLD_GLOBAL)
xdr = ctypes.CDLL("libgfxdr.so",ctypes.RTLD_GLOBAL)
api = ctypes.CDLL("api/libgfapi.so",ctypes.RTLD_GLOBAL)

fs = api.glfs_new(sys.argv[1])
api.glfs_set_logging(fs,"/dev/stderr",7)
api.glfs_set_volfile_server(fs,"socket","localhost",24007)
api.glfs_init(fs)
print "Initialized volume"

fd = api.glfs_creat(fs,sys.argv[2],os.O_RDWR,0644)
print "Created file"

# Read anything that's there from before.
rbuf = ctypes.create_string_buffer(32)
if api.glfs_read(fd,rbuf,32,0) > 0:
	print "old data = %s" % rbuf.value

# Write some new data.
api.glfs_lseek(fd,0,os.SEEK_SET)
wrote = api.glfs_write(fd,sys.argv[3],len(sys.argv[3]),0)
if wrote > 0:
	print "wrote %d bytes" % wrote
