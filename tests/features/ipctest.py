#!/usr/bin/python

import ctypes
import ctypes.util

api = ctypes.CDLL(ctypes.util.find_library("gfapi"))
api.glfs_ipc.argtypes = [ ctypes.c_void_p, ctypes.c_int ]
api.glfs_ipc.restype = ctypes.c_int

def do_ipc (host, volume):
	fs = api.glfs_new(volume)
	#api.glfs_set_logging(fs,"/dev/stderr",7)
	api.glfs_set_volfile_server(fs,"tcp",host,24007)

	api.glfs_init(fs)
	ret  = api.glfs_ipc(fs,1470369258)
	api.glfs_fini(fs)

	return ret

if __name__ == "__main__":
	import sys

	try:
		res = apply(do_ipc,sys.argv[1:3])
		print res
	except:
		print "IPC failed (volume not started?)"
