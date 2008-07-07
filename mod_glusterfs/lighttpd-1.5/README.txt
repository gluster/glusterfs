Introduction
============
mod_glusterfs is a module written for lighttpd to speed up the access of files present on glusterfs. mod_glusterfs uses libglusterfsclient library provided for glusterfs and hence can be used without fuse (File System in User Space).

Usage
=====
To use mod_glusterfs with lighttpd-1.5, copy mod_glusterfs.c and mod_glusterfs.h into src/ of lighttpd-1.5 source tree, and apply the Makefile.am.diff to src/Makefile.am. Re-run ./autogen.sh on the top level of the lighttpd-1.5 build tree and recompile.

# cp mod_glusterfs.[ch] /home/glusterfs/lighttpd-1.5/src/
# cd /home/glusterfs/lighttpd-1.5
# patch -p1 < Makefile.am.diff 
# ./autogen.sh
# ./configure
# make
# make install

Configuration
=============
* mod_glusterfs should be listed at the begining of the list server.modules in lighttpd.conf. 

Below is a snippet from lighttpd.conf concerning to mod_glusterfs.

$HTTP["url"] =~ "^/glusterfs" {
	glusterfs.prefix = "/glusterfs" 
	glusterfs.logfile = "/var/log/glusterfs-logfile"
	glusterfs.volume-specfile = "/etc/glusterfs/glusterfs.vol"
	glusterfs.loglevel = "error"
	glusterfs.cache-timeout = 300
	glusterfs.xattr-interface-size-limit = "65536"
}

* $HTTP["url"] =~ "^/glusterfs"
  A perl style regular expression used to match against the url. If regular expression matches the url, the url is handled by mod_glusterfs. Note that the pattern given here should match glusterfs.prefix.

* glusterfs.prefix (COMPULSORY)
  A string to be present at the starting of the file path in the url so that the file would be handled by glusterfs.
  Eg., A GET request on the url http://www.example.com/glusterfs-prefix/some-dir/example-file will result in fetching of the file "/some-dir/example-file" from glusterfs mount if glusterfs.prefix is set to "/glusterfs-prefix".

* glusterfs.volume-specfile (COMPULSORY)
  Path to the the glusterfs volume specification file.

* glusterfs.logfile (COMPULSORY)
  Path to the glusterfs logfile.

* glusterfs.loglevel (OPTIONAL, default = warning)
  Allowed values are critical, error, warning, debug, none in the decreasing order of severity of error conditions.

* glusterfs.cache-timeout (OPTIONAL, default = 0)
  Timeout values for glusterfs stat and lookup cache.

* glusterfs.document-root (COMPULSORY)
  An absolute path, relative to which all the files are fetched from glusterfs.

* glusterfs.xattr-interface-size-limit (OPTIONAL, default = 0)
  Files with sizes upto and including this value are fetched through the extended attribute interface of glusterfs rather than the usual open-read-close set of operations. For files of small sizes, it is recommended to use extended attribute interface.
