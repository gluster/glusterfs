#!/usr/bin/python

import string

ops = {}

ops['fgetxattr'] = (
	('fop-arg', 'fd',			'fd_t *'),
	('fop-arg',	'name',			'const char *'),
	('fop-arg',	'xdata',		'dict_t *'),
	('cbk-arg',	'dict',			'dict_t *'),
	('cbk-arg',	'xdata',		'dict_t *'),
)

ops['fsetxattr'] = (
	('fop-arg',	'fd',			'fd_t *'),
	('fop-arg',	'dict',			'dict_t *'),
	('fop-arg',	'flags',		'int32_t'),
	('fop-arg',	'xdata',		'dict_t *'),
	('cbk-arg',	'xdata',		'dict_t *'),
)

ops['setxattr'] = (
	('fop-arg',	'loc',			'loc_t *'),
	('fop-arg',	'dict',			'dict_t *'),
	('fop-arg',	'flags',		'int32_t'),
	('fop-arg',	'xdata',		'dict_t *'),
	('cbk-arg',	'xdata',		'dict_t *'),
)

ops['statfs'] = (
	('fop-arg',	'loc',			'loc_t *'),
	('fop-arg',	'xdata',		'dict_t *'),
	('cbk-arg',	'buf',			'struct statvfs *'),
	('cbk-arg',	'xdata',		'dict_t *'),
)

ops['fsyncdir'] = (
	('fop-arg',	'fd',			'fd_t *'),
	('fop-arg',	'flags',		'int32_t'),
	('fop-arg',	'xdata',		'dict_t *'),
	('cbk-arg',	'xdata',		'dict_t *'),
)

ops['opendir'] = (
	('fop-arg',	'loc',			'loc_t *'),
	('fop-arg',	'fd',			'fd_t *'),
	('fop-arg',	'xdata',		'dict_t *'),
	('cbk-arg',	'fd',			'fd_t *'),
	('cbk-arg',	'xdata',		'dict_t *'),
)

ops['fstat'] = (
	('fop-arg',	'fd',			'fd_t *'),
	('fop-arg',	'xdata',		'dict_t *'),
	('cbk-arg',	'buf',			'struct iatt *'),
	('cbk-arg',	'xdata',		'dict_t *'),
)

ops['fsync'] = (
	('fop-arg',	'fd',			'fd_t *'),
	('fop-arg',	'flags',		'int32_t'),
	('fop-arg',	'xdata',		'dict_t *'),
	('cbk-arg',	'prebuf',		'struct iatt *'),
	('cbk-arg',	'postbuf',		'struct iatt *'),
	('cbk-arg',	'xdata',		'dict_t *'),
)

ops['flush'] = (
	('fop-arg',	'fd',			'fd_t *'),
	('fop-arg',	'xdata',		'dict_t *'),
	('cbk-arg',	'xdata',		'dict_t *'),
)

ops['writev'] = (
	('fop-arg',	'fd',			'fd_t *'),
	('fop-arg',	'vector',		'struct iovec *'),
	('fop-arg',	'count',		'int32_t'),
	('fop-arg',	'off',			'off_t'),
	('fop-arg',	'flags',		'uint32_t'),
	('fop-arg',	'iobref',		'struct iobref *'),
	('fop-arg',	'xdata',		'dict_t *'),
	('cbk-arg',	'prebuf',		'struct iatt *'),
	('cbk-arg',	'postbuf',		'struct iatt *'),
	('cbk-arg',	'xdata',		'dict_t *'),
)

ops['readv'] = (
	('fop-arg',	'fd',			'fd_t *'),
	('fop-arg',	'size',			'size_t'),
	('fop-arg',	'offset',		'off_t'),
	('fop-arg',	'flags',		'uint32_t'),
	('fop-arg',	'xdata',		'dict_t *'),
	('cbk-arg',	'vector',		'struct iovec *'),
	('cbk-arg',	'count',		'int32_t'),
	('cbk-arg',	'stbuf',		'struct iatt *'),
	('cbk-arg',	'iobref',		'struct iobref *'),
	('cbk-arg',	'xdata',		'dict_t *'),
)

ops['open'] = (
	('fop-arg',	'loc',			'loc_t *'),
	('fop-arg',	'flags',		'int32_t'),
	('fop-arg',	'fd',			'fd_t *'),
	('fop-arg',	'xdata',		'dict_t *'),
	('cbk-arg',	'fd',			'fd_t *'),
	('cbk-arg',	'xdata',		'dict_t *'),
)

ops['create'] = (
	('fop-arg',	'loc',			'loc_t *'),
	('fop-arg',	'flags',		'int32_t'),
	('fop-arg',	'mode',			'mode_t'),
	('fop-arg',	'umask',		'mode_t'),
	('fop-arg',	'fd',			'fd_t *'),
	('fop-arg',	'xdata',		'dict_t *'),
	('cbk-arg',	'fd',			'fd_t *'),
	('cbk-arg',	'inode',		'inode_t *'),
	('cbk-arg',	'buf',			'struct iatt *'),
	('cbk-arg',	'preparent',	'struct iatt *'),
	('cbk-arg',	'postparent',	'struct iatt *'),
	('cbk-arg',	'xdata',		'dict_t *'),
)

ops['link'] = (
	('fop-arg',	'oldloc',		'loc_t *'),
	('fop-arg',	'newloc',		'loc_t *'),
	('fop-arg',	'xdata',		'dict_t *'),
	('cbk-arg',	'inode',		'inode_t *'),
	('cbk-arg',	'buf',			'struct iatt *'),
	('cbk-arg',	'preparent',	'struct iatt *'),
	('cbk-arg',	'postparent',	'struct iatt *'),
	('cbk-arg',	'xdata',		'dict_t *'),
)

ops['rename'] = (
	('fop-arg',	'oldloc',		'loc_t *'),
	('fop-arg',	'newloc',		'loc_t *'),
	('fop-arg',	'xdata',		'dict_t *'),
	('cbk-arg',	'buf',			'struct iatt *'),
	('cbk-arg',	'preoldparent',	'struct iatt *'),
	('cbk-arg',	'postoldparent','struct iatt *'),
	('cbk-arg',	'prenewparent',	'struct iatt *'),
	('cbk-arg',	'postnewparent','struct iatt *'),
	('cbk-arg',	'xdata',		'dict_t *'),
)

ops['symlink'] = (
	('fop-arg',	'linkpath',		'const char *'),
	('fop-arg',	'loc',			'loc_t *'),
	('fop-arg',	'umask',		'mode_t'),
	('fop-arg',	'xdata',		'dict_t *'),
	('cbk-arg',	'inode',		'inode_t *'),
	('cbk-arg',	'buf',			'struct iatt *'),
	('cbk-arg',	'preparent',	'struct iatt *'),
	('cbk-arg',	'postparent',	'struct iatt *'),
	('cbk-arg',	'xdata',		'dict_t *'),
)

ops['rmdir'] = (
	('fop-arg',	'loc',			'loc_t *'),
	('fop-arg',	'flags',		'int32_t'),
	('fop-arg',	'xdata',		'dict_t *'),
	('cbk-arg',	'preparent',	'struct iatt *'),
	('cbk-arg',	'postparent',	'struct iatt *'),
	('cbk-arg',	'xdata',		'dict_t *'),
)

ops['unlink'] = (
	('fop-arg',	'loc',			'loc_t *'),
	('fop-arg',	'flags',		'int32_t'),
	('fop-arg',	'xdata',		'dict_t *'),
	('cbk-arg',	'preparent',	'struct iatt *'),
	('cbk-arg',	'postparent',	'struct iatt *'),
	('cbk-arg',	'xdata',		'dict_t *'),
)

ops['mkdir'] = (
	('fop-arg',	'loc',			'loc_t *'),
	('fop-arg',	'mode',			'mode_t'),
	('fop-arg',	'umask',		'mode_t'),
	('fop-arg',	'xdata',		'dict_t *'),
	('cbk-arg',	'inode',		'inode_t *'),
	('cbk-arg',	'buf',			'struct iatt *'),
	('cbk-arg',	'preparent',	'struct iatt *'),
	('cbk-arg',	'postparent',	'struct iatt *'),
	('cbk-arg',	'xdata',		'dict_t *'),
)

ops['mknod'] = (
	('fop-arg',	'loc',			'loc_t *'),
	('fop-arg',	'mode',			'mode_t'),
	('fop-arg',	'rdev',			'dev_t'),
	('fop-arg',	'umask',		'mode_t'),
	('fop-arg',	'xdata',		'dict_t *'),
	('cbk-arg',	'inode',		'inode_t *'),
	('cbk-arg',	'buf',			'struct iatt *'),
	('cbk-arg',	'preparent',	'struct iatt *'),
	('cbk-arg',	'postparent',	'struct iatt *'),
	('cbk-arg',	'xdata',		'dict_t *'),
)

ops['readlink'] = (
	('fop-arg',	'loc',			'loc_t *'),
	('fop-arg',	'size',			'size_t'),
	('fop-arg',	'xdata',		'dict_t *'),
	('cbk-arg',	'path',			'const char *'),
	('cbk-arg',	'buf',			'struct iatt *'),
	('cbk-arg',	'xdata',		'dict_t *'),
)

ops['access'] = (
	('fop-arg',	'loc',			'loc_t *'),
	('fop-arg',	'mask',			'int32_t'),
	('fop-arg',	'xdata',		'dict_t *'),
	('cbk-arg',	'xdata',		'dict_t *'),
)

ops['ftruncate'] = (
	('fop-arg',	'fd',			'fd_t *'),
	('fop-arg',	'offset',		'off_t'),
	('fop-arg',	'xdata',		'dict_t *'),
	('cbk-arg',	'prebuf',		'struct iatt *'),
	('cbk-arg',	'postbuf',		'struct iatt *'),
	('cbk-arg',	'xdata',		'dict_t *'),
)

ops['getxattr'] = (
	('fop-arg',	'loc',			'loc_t *'),
	('fop-arg',	'name',			'const char *'),
	('fop-arg',	'xdata',		'dict_t *'),
	('cbk-arg',	'dict',			'dict_t *'),
	('cbk-arg',	'xdata',		'dict_t *'),
)

ops['xattrop'] = (
	('fop-arg',	'loc',			'loc_t *'),
	('fop-arg',	'flags',		'gf_xattrop_flags_t'),
	('fop-arg',	'dict',			'dict_t *'),
	('fop-arg',	'xdata',		'dict_t *'),
	('cbk-arg',	'dict',			'dict_t *'),
	('cbk-arg',	'xdata',		'dict_t *'),
)

ops['fxattrop'] = (
	('fop-arg',	'fd',			'fd_t *'),
	('fop-arg',	'flags',		'gf_xattrop_flags_t'),
	('fop-arg',	'dict',			'dict_t *'),
	('fop-arg',	'xdata',		'dict_t *'),
	('cbk-arg',	'dict',			'dict_t *'),
	('cbk-arg',	'xdata',		'dict_t *'),
)

ops['removexattr'] = (
	('fop-arg',	'loc',			'loc_t *'),
	('fop-arg',	'name',			'const char *'),
	('fop-arg',	'xdata',		'dict_t *'),
	('cbk-arg',	'xdata',		'dict_t *'),
)

ops['fremovexattr'] = (
	('fop-arg',	'fd',			'fd_t *'),
	('fop-arg',	'name',			'const char *'),
	('fop-arg',	'xdata',		'dict_t *'),
	('cbk-arg',	'xdata',		'dict_t *'),
)

ops['lk'] = (
	('fop-arg',	'fd',			'fd_t *'),
	('fop-arg',	'cmd',			'int32_t'),
	('fop-arg',	'lock',			'struct gf_flock *'),
	('fop-arg',	'xdata',		'dict_t *'),
	('cbk-arg',	'lock',			'struct gf_flock *'),
	('cbk-arg',	'xdata',		'dict_t *'),
)

ops['inodelk'] = (
	('fop-arg',	'volume',		'const char *'),
	('fop-arg',	'loc',			'loc_t *'),
	('fop-arg',	'cmd',			'int32_t'),
	('fop-arg',	'lock',			'struct gf_flock *'),
	('fop-arg',	'xdata',		'dict_t *'),
	('cbk-arg',	'xdata',		'dict_t *'),
)

ops['finodelk'] = (
	('fop-arg',	'volume',		'const char *'),
	('fop-arg',	'fd',			'fd_t *'),
	('fop-arg',	'cmd',			'int32_t'),
	('fop-arg',	'lock',			'struct gf_flock *'),
	('fop-arg',	'xdata',		'dict_t *'),
	('cbk-arg',	'xdata',		'dict_t *'),
)

ops['entrylk'] = (
	('fop-arg',	'volume',		'const char *'),
	('fop-arg',	'loc',			'loc_t *'),
	('fop-arg',	'basename',		'const char *'),
	('fop-arg',	'cmd',			'entrylk_cmd'),
	('fop-arg',	'type',			'entrylk_type'),
	('fop-arg',	'xdata',		'dict_t *'),
	('cbk-arg',	'xdata',		'dict_t *'),
)

ops['fentrylk'] = (
	('fop-arg',	'volume',		'const char *'),
	('fop-arg',	'fd',			'fd_t *'),
	('fop-arg',	'basename',		'const char *'),
	('fop-arg',	'cmd',			'entrylk_cmd'),
	('fop-arg',	'type',			'entrylk_type'),
	('fop-arg',	'xdata',		'dict_t *'),
	('cbk-arg',	'xdata',		'dict_t *'),
)

ops['rchecksum'] = (
	('fop-arg',	'fd',			'fd_t *'),
	('fop-arg',	'offset',		'off_t'),
	('fop-arg',	'len',			'int32_t'),
	('fop-arg',	'xdata',		'dict_t *'),
	('cbk-arg',	'weak_cksum',	'uint32_t'),
	('cbk-arg',	'strong_cksum',	'uint8_t *'),
	('cbk-arg',	'xdata',		'dict_t *'),
)

ops['readdir'] = (
	('fop-arg',	'fd',			'fd_t *'),
	('fop-arg',	'size',			'size_t'),
	('fop-arg',	'off',			'off_t'),
	('fop-arg',	'xdata',		'dict_t *'),
	('cbk-arg',	'entries',		'gf_dirent_t *'),
	('cbk-arg',	'xdata',		'dict_t *'),
)

ops['readdirp'] = (
	('fop-arg',	'fd',			'fd_t *'),
	('fop-arg',	'size',			'size_t'),
	('fop-arg',	'off',			'off_t'),
	('fop-arg',	'xdata',		'dict_t *'),
	('cbk-arg',	'entries',		'gf_dirent_t *'),
	('cbk-arg',	'xdata',		'dict_t *'),
)

ops['setattr'] = (
	('fop-arg',	'loc',			'loc_t *'),
	('fop-arg',	'stbuf',		'struct iatt *'),
	('fop-arg',	'valid',		'int32_t'),
	('fop-arg',	'xdata',		'dict_t *'),
	('cbk-arg',	'statpre',		'struct iatt *'),
	('cbk-arg',	'statpost',		'struct iatt *'),
	('cbk-arg',	'xdata',		'dict_t *'),
)

ops['truncate'] = (
	('fop-arg',	'loc',			'loc_t *'),
	('fop-arg',	'offset',		'off_t'),
	('fop-arg',	'xdata',		'dict_t *'),
	('cbk-arg',	'prebuf',		'struct iatt *'),
	('cbk-arg',	'postbuf',		'struct iatt *'),
	('cbk-arg',	'xdata',		'dict_t *'),
)

ops['stat'] = (
	('fop-arg',	'loc',			'loc_t *'),
	('fop-arg',	'xdata',		'dict_t *'),
	('cbk-arg',	'buf',			'struct iatt *'),
	('cbk-arg',	'xdata',		'dict_t *'),
)

ops['lookup'] = (
	('fop-arg',	'loc',			'loc_t *'),
	('fop-arg',	'xdata',		'dict_t *'),
	('cbk-arg',	'inode',		'inode_t *'),
	('cbk-arg',	'buf',			'struct iatt *'),
	('cbk-arg',	'xdata',		'dict_t *'),
	# We could add xdata everywhere automatically if somebody hadn't put
	# something after it here.
	('cbk-arg',	'postparent',	'struct iatt *'),
)

ops['fsetattr'] = (
	('fop-arg',	'fd',			'fd_t *'),
	('fop-arg',	'stbuf',		'struct iatt *'),
	('fop-arg',	'valid',		'int32_t'),
	('fop-arg',	'xdata',		'dict_t *'),
	('cbk-arg',	'statpre',		'struct iatt *'),
	('cbk-arg',	'statpost',		'struct iatt *'),
	('cbk-arg',	'xdata',		'dict_t *'),
)

ops['fallocate'] = (
	('fop-arg',	'fd',			'fd_t *'),
	('fop-arg',	'keep_size',	'int32_t'),
	('fop-arg',	'offset',		'off_t'),
	('fop-arg',	'len',			'size_t'),
	('fop-arg',	'xdata',		'dict_t *'),
	('cbk-arg',	'pre',			'struct iatt *'),
	('cbk-arg',	'post',			'struct iatt *'),
	('cbk-arg',	'xdata',		'dict_t *'),
)

ops['discard'] = (
	('fop-arg',	'fd',			'fd_t *'),
	('fop-arg',	'offset',		'off_t'),
	('fop-arg',	'len',			'size_t'),
	('fop-arg',	'xdata',		'dict_t *'),
	('cbk-arg',	'pre',			'struct iatt *'),
	('cbk-arg',	'post',			'struct iatt *'),
	('cbk-arg',	'xdata',		'dict_t *'),
)

ops['zerofill'] = (
	('fop-arg',	'fd',			'fd_t *'),
	('fop-arg',	'offset',		'off_t'),
	# As e.g. fallocate/discard (above) "len" should really be a size_t.
	('fop-arg',	'len',			'off_t'),
	('fop-arg',	'xdata',		'dict_t *'),
	('cbk-arg',	'pre',			'struct iatt *'),
	('cbk-arg',	'post',			'struct iatt *'),
	('cbk-arg',	'xdata',		'dict_t *'),
)

ops['ipc'] = (
	('fop-arg',	'op',			'int32_t'),
	('fop-arg',	'xdata',		'dict_t *'),
	('cbk-arg',	'xdata',		'dict_t *'),
)

ops['getspec'] = (
	('fop-arg',	'key',			'const char *'),
	('fop-arg',	'flags',		'int32_t'),
	('cbk-arg',	'spec_data',	'char *'),
)

def get_error_arg (type_str):
	if type_str.find(" *") != -1:
		return "NULL"
	return "-1"

def get_subs (names, types):
	sdict = {}
	sdict["@SHORT_ARGS@"] = string.join(names,", ")
	# Convert two separate tuples to one of (name, type) sub-tuples.
	as_tuples = zip(types,names)
	# Convert each sub-tuple into a "type name" string.
	as_strings = map(string.join,as_tuples)
	# Join all of those into one big string.
	sdict["@LONG_ARGS@"] = string.join(as_strings,",\n\t")
	# So much more readable than string.join(map(string.join,zip(...))))
	sdict["@ERROR_ARGS@"] = string.join(map(get_error_arg,types),", ")
	return sdict

def generate (tmpl, name, subs):
	text = tmpl.replace("@NAME@",name)
	for old, new in subs[name].iteritems():
		text = text.replace(old,new)
	# TBD: reindent/reformat the result for maximum readability.
	return  text

fop_subs = {}
cbk_subs = {}

for name, args in ops.iteritems():

	# Create the necessary substitution strings for fops.
	arg_names = [ a[1] for a in args if a[0] == 'fop-arg']
	arg_types = [ a[2] for a in args if a[0] == 'fop-arg']
	fop_subs[name] = get_subs(arg_names,arg_types)

	# Same thing for callbacks.
	arg_names = [ a[1] for a in args if a[0] == 'cbk-arg']
	arg_types = [ a[2] for a in args if a[0] == 'cbk-arg']
	cbk_subs[name] = get_subs(arg_names,arg_types)

	# Callers can add other subs to these tables, or even create their
	# own tables, using these same techniques, and then pass the result
	# to generate() which would Do The Right Thing with them.
