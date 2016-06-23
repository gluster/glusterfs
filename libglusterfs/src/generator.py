#!/usr/bin/python

import string

# ops format: 'fop-arg' name type stub-field [nosync]
#             'cbk-arg' name type
#             'extra'   name type arg-str
#             'journal' fop-type
#             'link'    inode iatt
#
# 'role' indicates the significance of this line to the code generator (sort of
# our own type).
#
# For fop-arg, we first need to know the name and the type of the arg so that
# we can generate SHORT_ARGS (for function calls) and LONG_ARGS (for
# declarations).  For code that uses stubs, we also need to know the name of
# the stub field, which might be different than the argument itself.  Lastly,
# for code that uses syncops, we need to know whether whoever wrote the syncop
# for this fop "forgot" to include this argument.  (Editorial: this kind of
# creeping inconsistency is why we should have used code generation for stubs
# and syncops as well as defaults all along.)  To address this need, we use the
# optional 'nosync' field for arguments (e.g. mkdir.umask) that we should skip
# in generated syncop code.
#
# 'cbk-arg' is like fop-arg but simpler and used for generating callbacks
# instead of fop functions.
#
# 'extra' is also like fop-arg, but it's another hack for syncops.  This time
# the problem is that some of what would normally be *callback* arguments are
# instead created in the caller and passed to the syncop.  We handle that by
# adding an entry at the appropriate place in the fop-arg list, with the name
# and type to generate a declaration and an argument string to generate the
# actual syncop call.
#
# The mere presence of a 'journal' item is sufficient for most of the journal
# code to recognize that it should do something.  However, reconciliation also
# needs to decide how reconciliation builds the arguments it needs to call down
# to the syncop layer, based on what's in the journal.  To do that, we divide
# ops into three types and store those types in the ops table.  In general,
# these three types work as follows.
#
#    For an fd-op, the GFID in the journal is used (in loc.gfid) field to
#    look up an inode, then an anonymous fd is found/created for that inode.
#
#    For an inode-op, the GFID in the journal is used the same way, but no fd
#    is needed.
#
#    For an entry-op, the *parent* GFID and name from the journal are used to
#    look up an inode (via loc.pargfid and par.name respectively).
#
# The only places this seems to fall down is for link and create.  In link,
# which is generally an entry-op, the source is looked up as though it's an
# inode-op.  In create, we have an fd argument but it's really a return
# argument so we get a fresh inode instead of looking one up.  Those two cases
# need to be handled as special cases in the reconciliation code.
#
# 'link' is (hopefully) the last of the journal/syncop hacks.  Much like
# 'extra', some values that are returned as callback arguments in the normal
# case are handled differently for syncops.  For syncops that create objects
# (e.g. mkdir) we need to link those objects into our inode table.  The 'inode'
# and 'iatt' fields here give us the information we need to construct the
# proper inode_link call(s).

ops = {}
xlator_cbks = {}
xlator_dumpops = {}

ops['fgetxattr'] = (
	('fop-arg', 'fd',			'fd_t *'),
	('fop-arg',	'name',			'const char *'),
	('fop-arg',	'xdata',		'dict_t *'),
	('cbk-arg',	'dict',			'dict_t *'),
	('cbk-arg',	'xdata',		'dict_t *'),
)

ops['fsetxattr'] = (
	('fop-arg',	'fd',			'fd_t *',			'fd'),
	('fop-arg',	'dict',			'dict_t *',			'xattr'),
	('fop-arg',	'flags',		'int32_t',			'flags'),
	('fop-arg',	'xdata',		'dict_t *',			'xdata'),
	('cbk-arg',	'xdata',		'dict_t *'),
	('journal',	'fd-op'),
)

ops['setxattr'] = (
	('fop-arg',	'loc',			'loc_t *',			'loc'),
	('fop-arg',	'dict',			'dict_t *',			'xattr'),
	('fop-arg',	'flags',		'int32_t',			'flags'),
	('fop-arg',	'xdata',		'dict_t *',			'xdata'),
	('cbk-arg',	'xdata',		'dict_t *'),
	('journal',	'inode-op'),
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
	('fop-arg',	'fd',			'fd_t *',			'fd'),
	('fop-arg',	'vector',		'struct iovec *',	'vector'),
	('fop-arg',	'count',		'int32_t'),
	('fop-arg',	'off',			'off_t',			'offset'),
	('fop-arg',	'flags',		'uint32_t',			'flags'),
	('fop-arg',	'iobref',		'struct iobref *'),
	('fop-arg',	'xdata',		'dict_t *',			'xdata'),
	('cbk-arg',	'prebuf',		'struct iatt *'),
	('cbk-arg',	'postbuf',		'struct iatt *'),
	('cbk-arg',	'xdata',		'dict_t *'),
	('journal',	'fd-op'),
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
	('fop-arg',	'loc',			'loc_t *',			'loc'),
	('fop-arg',	'flags',		'int32_t',			'flags'),
	('fop-arg',	'mode',			'mode_t',			'mode'),
	('fop-arg',	'umask',		'mode_t',			'umask',	'nosync'),
	('fop-arg',	'fd',			'fd_t *',			'fd'),
	('extra',	'iatt',			'struct iatt',		'&iatt'),
	('fop-arg',	'xdata',		'dict_t *',			'xdata'),
	('cbk-arg',	'fd',			'fd_t *'),
	('cbk-arg',	'inode',		'inode_t *'),
	('cbk-arg',	'buf',			'struct iatt *'),
	('cbk-arg',	'preparent',	'struct iatt *'),
	('cbk-arg',	'postparent',	'struct iatt *'),
	('cbk-arg',	'xdata',		'dict_t *'),
	('journal',	'entry-op'),
	('link',	'loc.inode',	'&iatt'),
)

ops['link'] = (
	('fop-arg',	'oldloc',		'loc_t *',			'loc'),
	('fop-arg',	'newloc',		'loc_t *',			'loc2'),
	('extra',	'iatt',			'struct iatt',		'&iatt'),
	('fop-arg',	'xdata',		'dict_t *',			'xdata'),
	('cbk-arg',	'inode',		'inode_t *'),
	('cbk-arg',	'buf',			'struct iatt *'),
	('cbk-arg',	'preparent',	'struct iatt *'),
	('cbk-arg',	'postparent',	'struct iatt *'),
	('cbk-arg',	'xdata',		'dict_t *'),
	('journal',	'entry-op'),
)

ops['rename'] = (
	('fop-arg',	'oldloc',		'loc_t *',			'loc'),
	('fop-arg',	'newloc',		'loc_t *',			'loc2'),
	('fop-arg',	'xdata',		'dict_t *',			'xdata'),
	('cbk-arg',	'buf',			'struct iatt *'),
	('cbk-arg',	'preoldparent',	'struct iatt *'),
	('cbk-arg',	'postoldparent','struct iatt *'),
	('cbk-arg',	'prenewparent',	'struct iatt *'),
	('cbk-arg',	'postnewparent','struct iatt *'),
	('cbk-arg',	'xdata',		'dict_t *'),
	('journal',	'entry-op'),
)

ops['symlink'] = (
	('fop-arg',	'linkpath',		'const char *',		'linkname'),
	('fop-arg',	'loc',			'loc_t *',			'loc'),
	('fop-arg',	'umask',		'mode_t',			'mode',		'nosync'),
	('extra',	'iatt',			'struct iatt',		'&iatt'),
	('fop-arg',	'xdata',		'dict_t *',			'xdata'),
	('cbk-arg',	'inode',		'inode_t *'),
	('cbk-arg',	'buf',			'struct iatt *'),
	('cbk-arg',	'preparent',	'struct iatt *'),
	('cbk-arg',	'postparent',	'struct iatt *'),
	('cbk-arg',	'xdata',		'dict_t *'),
	('journal',	'entry-op'),
)

ops['rmdir'] = (
	('fop-arg',	'loc',			'loc_t *',			'loc'),
	('fop-arg',	'flags',		'int32_t',			'flags'),
	('fop-arg',	'xdata',		'dict_t *',			'xdata'),
	('cbk-arg',	'preparent',	'struct iatt *'),
	('cbk-arg',	'postparent',	'struct iatt *'),
	('cbk-arg',	'xdata',		'dict_t *'),
	('journal',	'entry-op'),
)

ops['unlink'] = (
	('fop-arg',	'loc',			'loc_t *',			'loc'),
	('fop-arg',	'flags',		'int32_t',			'flags',	'nosync'),
	('fop-arg',	'xdata',		'dict_t *',			'xdata'),
	('cbk-arg',	'preparent',	'struct iatt *'),
	('cbk-arg',	'postparent',	'struct iatt *'),
	('cbk-arg',	'xdata',		'dict_t *'),
	('journal',	'entry-op'),
)

ops['mkdir'] = (
	('fop-arg',	'loc',			'loc_t *',			'loc'),
	('fop-arg',	'mode',			'mode_t',			'mode'),
	('fop-arg',	'umask',		'mode_t',			'umask',	'nosync'),
	('extra',	'iatt',			'struct iatt',		'&iatt'),
	('fop-arg',	'xdata',		'dict_t *',			'xdata'),
	('cbk-arg',	'inode',		'inode_t *'),
	('cbk-arg',	'buf',			'struct iatt *'),
	('cbk-arg',	'preparent',	'struct iatt *'),
	('cbk-arg',	'postparent',	'struct iatt *'),
	('cbk-arg',	'xdata',		'dict_t *'),
	('journal',	'entry-op'),
	('link',	'loc.inode',	'&iatt'),
)

ops['mknod'] = (
	('fop-arg',	'loc',			'loc_t *',			'loc'),
	('fop-arg',	'mode',			'mode_t',			'mode'),
	('fop-arg',	'rdev',			'dev_t',			'rdev'),
	('fop-arg',	'umask',		'mode_t',			'umask',	'nosync'),
	('extra',	'iatt',			'struct iatt',		'&iatt'),
	('fop-arg',	'xdata',		'dict_t *',			'xdata'),
	('cbk-arg',	'inode',		'inode_t *'),
	('cbk-arg',	'buf',			'struct iatt *'),
	('cbk-arg',	'preparent',	'struct iatt *'),
	('cbk-arg',	'postparent',	'struct iatt *'),
	('cbk-arg',	'xdata',		'dict_t *'),
	('journal',	'entry-op'),
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
	('fop-arg',	'fd',			'fd_t *',				'fd'),
	('fop-arg',	'offset',		'off_t',				'offset'),
	('fop-arg',	'xdata',		'dict_t *',				'xdata'),
	('cbk-arg',	'prebuf',		'struct iatt *'),
	('cbk-arg',	'postbuf',		'struct iatt *'),
	('cbk-arg',	'xdata',		'dict_t *'),
	('journal',	'fd-op'),
)

ops['getxattr'] = (
	('fop-arg',	'loc',			'loc_t *'),
	('fop-arg',	'name',			'const char *'),
	('fop-arg',	'xdata',		'dict_t *'),
	('cbk-arg',	'dict',			'dict_t *'),
	('cbk-arg',	'xdata',		'dict_t *'),
)

ops['xattrop'] = (
	('fop-arg',	'loc',			'loc_t *',				'loc'),
	('fop-arg',	'flags',		'gf_xattrop_flags_t',	'optype'),
	('fop-arg',	'dict',			'dict_t *',				'xattr'),
	('fop-arg',	'xdata',		'dict_t *',				'xdata'),
	('cbk-arg',	'dict',			'dict_t *'),
	('cbk-arg',	'xdata',		'dict_t *'),
	('journal',	'inode-op'),
)

ops['fxattrop'] = (
	('fop-arg',	'fd',			'fd_t *',				'fd'),
	('fop-arg',	'flags',		'gf_xattrop_flags_t',	'optype'),
	('fop-arg',	'dict',			'dict_t *',				'xattr'),
	('fop-arg',	'xdata',		'dict_t *',				'xdata'),
	('cbk-arg',	'dict',			'dict_t *'),
	('cbk-arg',	'xdata',		'dict_t *'),
	('journal',	'fd-op'),
)

ops['removexattr'] = (
	('fop-arg',	'loc',			'loc_t *',			'loc'),
	('fop-arg',	'name',			'const char *',		'name'),
	('fop-arg',	'xdata',		'dict_t *',			'xdata'),
	('cbk-arg',	'xdata',		'dict_t *'),
	('journal',	'inode-op'),
)

ops['fremovexattr'] = (
	('fop-arg',	'fd',			'fd_t *',			'fd'),
	('fop-arg',	'name',			'const char *',		'name'),
	('fop-arg',	'xdata',		'dict_t *',			'xdata'),
	('cbk-arg',	'xdata',		'dict_t *'),
	('journal',	'fd-op'),
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
	('fop-arg',	'loc',			'loc_t *',			'loc'),
	('fop-arg',	'stbuf',		'struct iatt *',	'stat'),
	('fop-arg',	'valid',		'int32_t',			'valid'),
	('extra',	'preop',		'struct iatt',		'&preop'),
	('extra',	'postop',		'struct iatt',		'&postop'),
	('fop-arg',	'xdata',		'dict_t *',			'xdata'),
	('cbk-arg',	'statpre',		'struct iatt *'),
	('cbk-arg',	'statpost',		'struct iatt *'),
	('cbk-arg',	'xdata',		'dict_t *'),
	('journal',	'inode-op'),
)

ops['truncate'] = (
	('fop-arg',	'loc',			'loc_t *',			'loc'),
	('fop-arg',	'offset',		'off_t',			'offset'),
	('fop-arg',	'xdata',		'dict_t *',			'xdata'),
	('cbk-arg',	'prebuf',		'struct iatt *'),
	('cbk-arg',	'postbuf',		'struct iatt *'),
	('cbk-arg',	'xdata',		'dict_t *'),
	('journal',	'inode-op'),
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
	('fop-arg',	'fd',			'fd_t *',			'fd'),
	('fop-arg',	'stbuf',		'struct iatt *',	'stat'),
	('fop-arg',	'valid',		'int32_t',			'valid'),
	('extra',	'preop',		'struct iatt',		'&preop'),
	('extra',	'postop',		'struct iatt',		'&postop'),
	('fop-arg',	'xdata',		'dict_t *',			'xdata'),
	('cbk-arg',	'statpre',		'struct iatt *'),
	('cbk-arg',	'statpost',		'struct iatt *'),
	('cbk-arg',	'xdata',		'dict_t *'),
	('journal',	'fd-op'),
)

ops['fallocate'] = (
	('fop-arg',	'fd',			'fd_t *',			'fd'),
	('fop-arg',	'keep_size',	'int32_t',			'mode'),
	('fop-arg',	'offset',		'off_t',			'offset'),
	('fop-arg',	'len',			'size_t',			'size'),
	('fop-arg',	'xdata',		'dict_t *',			'xdata'),
	('cbk-arg',	'pre',			'struct iatt *'),
	('cbk-arg',	'post',			'struct iatt *'),
	('cbk-arg',	'xdata',		'dict_t *'),
	('journal',	'fd-op'),
)

ops['discard'] = (
	('fop-arg',	'fd',			'fd_t *',			'fd'),
	('fop-arg',	'offset',		'off_t',			'offset'),
	('fop-arg',	'len',			'size_t',			'size'),
	('fop-arg',	'xdata',		'dict_t *',			'xdata'),
	('cbk-arg',	'pre',			'struct iatt *'),
	('cbk-arg',	'post',			'struct iatt *'),
	('cbk-arg',	'xdata',		'dict_t *'),
	('journal',	'fd-op'),
)

ops['zerofill'] = (
	('fop-arg',	'fd',			'fd_t *',			'fd'),
	('fop-arg',	'offset',		'off_t',			'offset'),
	# As e.g. fallocate/discard (above) "len" should really be a size_t.
	('fop-arg',	'len',			'off_t',			'size'),
	('fop-arg',	'xdata',		'dict_t *',			'xdata'),
	('cbk-arg',	'pre',			'struct iatt *'),
	('cbk-arg',	'post',			'struct iatt *'),
	('cbk-arg',	'xdata',		'dict_t *'),
	('journal',	'fd-op'),
)

ops['ipc'] = (
	('fop-arg',	'op',			'int32_t'),
	('fop-arg',	'xdata',		'dict_t *',			'xdata'),
	('cbk-arg',	'xdata',		'dict_t *'),
	('journal',	'fd-op'),
)

ops['seek'] = (
	('fop-arg',	'fd',			'fd_t *'),
	('fop-arg',	'offset',		'off_t'),
	('fop-arg',	'what',			'gf_seek_what_t'),
	('fop-arg',	'xdata',		'dict_t *'),
	('cbk-arg',	'offset',		'off_t'),
	('cbk-arg',	'xdata',		'dict_t *'),
)

ops['getspec'] = (
	('fop-arg',	'key',			'const char *'),
	('fop-arg',	'flags',		'int32_t'),
	('cbk-arg',	'spec_data',	'char *'),
)

ops['lease'] = (
        ('fop-arg',     'loc',                  'loc_t *'),
        ('fop-arg',     'lease',                'struct gf_lease *'),
        ('fop-arg',     'xdata',                'dict_t *'),
        ('cbk-arg',     'lease',                'struct gf_lease *'),
        ('cbk-arg',     'xdata',                'dict_t *'),
)

ops['getactivelk'] = (
        ('fop-arg',     'loc',                  'loc_t *'),
        ('fop-arg',     'xdata',                'dict_t *'),
        ('cbk-arg',     'locklist',             'lock_migration_info_t *'),
        ('cbk-arg',     'xdata',                'dict_t *'),
)

ops['setactivelk'] = (
        ('fop-arg',     'loc',                  'loc_t *'),
        ('fop-arg',     'locklist',             'lock_migration_info_t *'),
        ('fop-arg',     'xdata',                'dict_t *'),
        ('cbk-arg',     'xdata',                'dict_t *'),
)

#####################################################################
xlator_cbks['forget'] = (
        ('fn-arg',      'this',        'xlator_t *'),
        ('fn-arg',      'inode',       'inode_t *'),
	('ret-val',     'int32_t',     '0'),
)

xlator_cbks['release'] = (
        ('fn-arg',      'this',        'xlator_t *'),
        ('fn-arg',      'fd',          'fd_t *'),
        ('ret-val',     'int32_t',     '0'),
)

xlator_cbks['releasedir'] = (
        ('fn-arg',      'this',        'xlator_t *'),
        ('fn-arg',      'fd',          'fd_t *'),
        ('ret-val',     'int32_t',     '0'),
)

xlator_cbks['invalidate'] = (
        ('fn-arg',      'this',        'xlator_t *'),
        ('fn-arg',      'inode',       'inode_t *'),
        ('ret-val',     'int32_t',     '0'),
)

xlator_cbks['client_destroy'] = (
        ('fn-arg',      'this',        'xlator_t *'),
        ('fn-arg',      'client',      'client_t *'),
        ('ret-val',     'int32_t',     '0'),
)

xlator_cbks['client_disconnect'] = (
        ('fn-arg',      'this',        'xlator_t *'),
        ('fn-arg',      'client',      'client_t *'),
        ('ret-val',     'int32_t',     '0'),
)

xlator_cbks['ictxmerge'] = (
        ('fn-arg',      'this',        'xlator_t *'),
        ('fn-arg',      'fd',          'fd_t *'),
        ('fn-arg',      'inode',       'inode_t *'),
        ('fn-arg',      'linked_inode', 'inode_t *'),
        ('ret-val',     'void',        ''),
)

#####################################################################
xlator_dumpops['priv'] = (
        ('fn-arg',      'this',        'xlator_t *'),
        ('ret-val',     'int32_t',     '0'),
)

xlator_dumpops['inode'] = (
        ('fn-arg',      'this',        'xlator_t *'),
        ('ret-val',     'int32_t',     '0'),
)

xlator_dumpops['fd'] = (
        ('fn-arg',      'this',        'xlator_t *'),
        ('ret-val',     'int32_t',     '0'),
)

xlator_dumpops['inodectx'] = (
        ('fn-arg',      'this',        'xlator_t *'),
        ('fn-arg',      'ino',         'inode_t *'),
        ('ret-val',     'int32_t',     '0'),
)

xlator_dumpops['fdctx'] = (
        ('fn-arg',      'this',        'xlator_t *'),
        ('fn-arg',      'fd',          'fd_t *'),
        ('ret-val',     'int32_t',     '0'),
)

xlator_dumpops['priv_to_dict'] = (
        ('fn-arg',      'this',        'xlator_t *'),
        ('fn-arg',      'dict',        'dict_t *'),
        ('ret-val',     'int32_t',     '0'),
)

xlator_dumpops['inode_to_dict'] = (
        ('fn-arg',      'this',        'xlator_t *'),
        ('fn-arg',      'dict',        'dict_t *'),
        ('ret-val',     'int32_t',     '0'),
)

xlator_dumpops['fd_to_dict'] = (
        ('fn-arg',      'this',        'xlator_t *'),
        ('fn-arg',      'dict',        'dict_t *'),
        ('ret-val',     'int32_t',     '0'),
)

xlator_dumpops['inodectx_to_dict'] = (
        ('fn-arg',      'this',        'xlator_t *'),
        ('fn-arg',      'ino',         'inode_t *'),
        ('fn-arg',      'dict',        'dict_t *'),
        ('ret-val',     'int32_t',     '0'),
)

xlator_dumpops['fdctx_to_dict'] = (
        ('fn-arg',      'this',        'xlator_t *'),
        ('fn-arg',      'fd',          'fd_t *'),
        ('fn-arg',      'dict',        'dict_t *'),
        ('ret-val',     'int32_t',     '0'),
)

xlator_dumpops['history'] = (
        ('fn-arg',      'this',        'xlator_t *'),
        ('ret-val',     'int32_t',     '0'),
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
	if name == "writev":
		# More spurious inconsistency.
		text = text.replace("@UPNAME@","WRITE")
	else:
		text = text.replace("@UPNAME@",name.upper())
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
