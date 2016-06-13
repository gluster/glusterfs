#!/usr/bin/python

import ctypes
import ctypes.util

# find_library does not lookup LD_LIBRARY_PATH and may miss the
# function. In that case, retry with less portable but explicit name.
libgfapi = ctypes.util.find_library("gfapi")
if libgfapi == None:
	libgfapi = "libgfapi.so"
api = ctypes.CDLL(libgfapi,mode=ctypes.RTLD_GLOBAL)

api.glfs_ipc.argtypes = [ ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p ]
api.glfs_ipc.restype = ctypes.c_int

def do_ipc (host, volume):
	fs = api.glfs_new(volume)
	#api.glfs_set_logging(fs,"/dev/stderr",7)
	api.glfs_set_volfile_server(fs,"tcp",host,24007)

	api.glfs_init(fs)
	ret  = api.glfs_ipc(fs,1470369258,0,0)
	api.glfs_fini(fs)

	return ret

if __name__ == "__main__":
	import sys

	try:
		res = apply(do_ipc,sys.argv[1:3])
		print res
	except:
		print "IPC failed (volume not started?)"
