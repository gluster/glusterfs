import sys
from ctypes import *

dl = CDLL("",RTLD_GLOBAL)

class call_frame_t (Structure):
	pass

class dict_t (Structure):
	pass

class fd_t (Structure):
	pass

class iatt_t (Structure):
	pass

class inode_t (Structure):
	pass

class loc_t (Structure):
	_fields_ = [
		( "path",	c_char_p ),
		( "name",	c_char_p ),
		( "inode",	c_void_p ),
		( "parent",	c_void_p ),
		# Not quite correct, but easier to manipulate.
		( "gfid", c_uint * 4 ),
		( "pargfid", c_uint * 4 ),
	]

class xlator_t (Structure):
	pass

def _init_op (a_class, fop, cbk, wind, unwind):
	# Decorators, used by translators. We could pass the signatures as
	# parameters, but it's actually kind of nice to keep them around for
	# inspection.
	a_class.fop_type = apply(CFUNCTYPE,a_class.fop_sig)
	a_class.cbk_type = apply(CFUNCTYPE,a_class.cbk_sig)
	# Dispatch-function registration.
	fop.restype = None
	fop.argtypes = [ c_long, a_class.fop_type ]
	# Callback-function registration.
	cbk.restype = None
	cbk.argtypes = [ c_long, a_class.cbk_type ]
	# STACK_WIND function.
	wind.restype = None
	wind.argtypes = list(a_class.fop_sig[1:])
	# STACK_UNWIND function.
	unwind.restype = None
	unwind.argtypes = list(a_class.cbk_sig[1:])

class OpLookup:
	fop_sig = (c_int, POINTER(call_frame_t), POINTER(xlator_t),
		   POINTER(loc_t), POINTER(dict_t))
	cbk_sig = (c_int, POINTER(call_frame_t), c_long, POINTER(xlator_t),
		   c_int, c_int, POINTER(inode_t), POINTER(iatt_t),
		   POINTER(dict_t), POINTER(iatt_t))
_init_op (OpLookup, dl.set_lookup_fop, dl.set_lookup_cbk,
		    dl.wind_lookup,    dl.unwind_lookup)

class OpCreate:
	fop_sig = (c_int, POINTER(call_frame_t), POINTER(xlator_t),
		   POINTER(loc_t), c_int, c_uint, c_uint, POINTER(fd_t),
		   POINTER(dict_t))
	cbk_sig = (c_int, POINTER(call_frame_t), c_long, POINTER(xlator_t),
		   c_int, c_int, POINTER(fd_t), POINTER(inode_t),
		   POINTER(iatt_t), POINTER(iatt_t), POINTER(iatt_t),
		   POINTER(dict_t))
_init_op (OpCreate, dl.set_create_fop, dl.set_create_cbk,
		    dl.wind_create,    dl.unwind_create)

class Translator:
	def __init__ (self, c_this):
		# This is only here to keep references to the stubs we create,
		# because ctypes doesn't and glupy.so can't because it doesn't
		# get a pointer to the actual Python object. It's a dictionary
		# instead of a list in case we ever allow changing fops/cbks
		# after initialization and need to look them up.
		self.stub_refs = {}
		funcs = dir(self.__class__)
		if "lookup_fop" in funcs:
			@OpLookup.fop_type
			def stub (frame, this, loc, xdata, s=self):
				return s.lookup_fop (frame, this, loc, xdata)
			self.stub_refs["lookup_fop"] = stub
			dl.set_lookup_fop(c_this,stub)
		if "lookup_cbk" in funcs:
			@OpLookup.cbk_type
			def stub (frame, cookie, this, op_ret, op_errno, inode,
				  buf, xdata, postparent, s=self):
				return s.lookup_cbk(frame, cookie, this, op_ret,
						    op_errno, inode, buf, xdata,
						    postparent)
			self.stub_refs["lookup_cbk"] = stub
			dl.set_lookup_cbk(c_this,stub)
		if "create_fop" in funcs:
			@OpCreate.fop_type
			def stub (frame, this, loc, flags, mode, umask, fd,
				  xdata, s=self):
				return s.create_fop (frame, this, loc, flags,
						     mode, umask, fd, xdata)
			self.stub_refs["create_fop"] = stub
			dl.set_create_fop(c_this,stub)
		if "create_cbk" in funcs:
			@OpCreate.cbk_type
			def stub (frame, cookie, this, op_ret, op_errno, fd,
				  inode, buf, preparent, postparent, xdata,
				  s=self):
				return s.create_cbk (frame, cookie, this,
						     op_ret, op_errno, fd,
						     inode, buf, preparent,
						     postparent, xdata)
				return 0
			self.stub_refs["create_cbk"] = stub
			dl.set_create_cbk(c_this,stub)

