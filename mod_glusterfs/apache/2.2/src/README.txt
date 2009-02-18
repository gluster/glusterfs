What is mod_glusterfs?
======================
* mod_glusterfs is a module for apache written for efficient serving of files from glusterfs. 
  mod_glusterfs interfaces with glusterfs using apis provided by libglusterfsclient.

* this README speaks about installing mod_glusterfs for httpd-2.2 and higher.

Prerequisites for mod_glusterfs
===============================
Though mod_glusterfs has been written as a module, with an intent of making no changes to 
the way apache has been built, currently following points have to be taken care of:

* since glusterfs is compiled with _FILE_OFFSET_BITS=64 and __USE_FILE_OFFSET64 flags, mod_glusterfs and apache 
  in turn have to be compiled with the above two flags.
 
 $ tar xzf httpd-2.2.10.tar.gz 
 $ cd httpd-2.2.10/
 $ export CFLAGS='-D_FILE_OFFSET_BITS=64 -D__USE_FILE_OFFSET64'
 $ ./configure --prefix=/usr 
 $ make 
 $ make install
 $ httpd -l | grep -i mod_so 
   mod_so.c

* if multiple apache installations are present, make sure to pass --with-apxs=/path/to/apxs/of/proper/version 
  to configure script while building glusterfs.

Build/Install mod_glusterfs
===========================
* mod_glusterfs is provided with glusterfs--mainline--3.0 and all releases from the same branch.

* building glusterfs also builds mod_glusterfs. But 'make install' of glusterfs installs mod_glusterfs.so to 
  glusterfs install directory instead of the apache modules directory.

* 'make install' of glusterfs will print a message similar to the one given below, which is self explanatory. 
  Make sure to use apxs of proper apache version in case of multiple apache installations. This will copy 
  mod_glusterfs.so to modules directory of proper apache version and modify the appropriate httpd.conf to enable
  mod_glusterfs. 

**********************************************************************************
* TO INSTALL MODGLUSTERFS, PLEASE USE,                                            
* apxs -n glusterfs -ia /usr/lib/glusterfs/1.4.0tla872/apache/2.2/mod_glusterfs.so                
**********************************************************************************

Configuration
=============
* Following configuration has to be added to httpd.conf.

 <Location "/glusterfs">
 	   GlusterfsLogfile "/var/log/glusterfs/glusterfs.log"
	   GlusterfsLoglevel "warning"
 	   GlusterfsVolumeSpecfile "/etc/glusterfs/glusterfs-client.spec"
	   GlusterfsCacheTimeout "600"
	   GlusterfsXattrFileSize "65536"
 	   SetHandler "glusterfs-handler"
 </Location>

* GlusterfsVolumeSpecfile (COMPULSORY)
  Path to the the glusterfs volume specification file.

* GlusterfsLogfile (COMPULSORY)
  Path to the glusterfs logfile.

* GlusterfsLoglevel (OPTIONAL, default = warning)
  Severity of messages that are to be logged. Allowed values are critical, error, warning, debug, none 
  in the decreasing order of severity.

* GlusterfsCacheTimeOut (OPTIONAL, default = 0)
  Timeout values for glusterfs stat and lookup cache.

* GlusterfsXattrFileSize (OPTIONAL, default = 0)
  Files with sizes upto and including this value are fetched through the extended attribute interface of 
  glusterfs rather than the usual open-read-close set of operations. For files of small sizes, it is recommended 
  to use extended attribute interface.

* With the above configuration all the requests to httpd of the form www.example.org/glusterfs/path/to/file are 
  served from glusterfs.

* mod_glusterfs also implements mod_dir and mod_autoindex behaviour for files under glusterfs mount.
  Hence it also takes the directives related to both of these modules. For more details, refer the 
  documentation for both of these modules. 

Miscellaneous points
====================
* httpd by default runs with username "nobody" and group "nogroup". Permissions of logfile and specfile have to 
  be set suitably.

* Since mod_glusterfs runs with permissions of nobody.nogroup, glusterfs has to use only login based 
  authentication. See docs/authentication.txt for more details. 

* To copy the data served by httpd into glusterfs mountpoint, glusterfs can be started with the 
  volume-specification file provided to mod_glusterfs. Any tool like cp can then be used.

* To run in gdb, apache has to be compiled with -lpthread, since libglusterfsclient is 
  multithreaded. If not on Linux gdb runs into errors like: 
  "Error while reading shared library symbols:
   Cannot find new threads: generic error"

* when used with ib-verbs transport, ib_verbs initialization fails.
  reason for this is that apache runs as non-privileged user and the amount of memory that can be 
  locked by default is not sufficient for ib-verbs. to fix this, as root run,
  
  # ulimit -l unlimited

  and then start apache.
