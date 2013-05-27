gsycnd, the Gluster Syncdaemon
==============================

REQUIREMENTS
------------

_gsyncd_ is a program which can operate either in _master_ or in _slave_ mode.
Requirements are categorized according to this.

* supported OS is GNU/Linux
* Python >= 2.5, or 2.4 with Ctypes (see below) (both)
* OpenSSH >= 4.0 (master) / SSH2 compliant sshd (eg. openssh) (slave)
* rsync (both)
* glusterfs with marker support (master); glusterfs (optional on slave)
* FUSE; for supported versions consult glusterfs

INSTALLATION
------------

As of now, the supported way of operation is running from the source directory.

If you use Python 2.4.x, you need to install the [Ctypes module](http://python.net/crew/theller/ctypes/).

CONFIGURATION
-------------

gsyncd tunables are a subset of the long command-line options; for listing them,
type

    gsyncd.py --help

and see the long options up to "--config-file". (The leading double dash should be omitted;
interim underscores and dashes are interchangeable.) The set of options bear some resemblance
to those of glusterfs and rsync.

The config file format matches the following syntax:

      <option1>: <value1>
      <option2>: <value2>
      # comment

By default (unless specified by the option `-c`), gsyncd looks for config file at _conf/gsyncd.conf_
in the source tree.

USAGE
-----

gsyncd is a utilitly for continous mirroring, ie. it mirrors master to slave incrementally.
Assume we have a gluster volume _pop_ at localhost. We try to set up the following mirrors
for it with gysncd:

1. _/data/mirror_
2. local gluster volume _yow_
3. _/data/far_mirror_ at example.com
4. gluster volume _moz_ at example.com

The respective gsyncd invocations are (demoing some syntax sugaring):

1.

      gsyncd.py gluster://localhost:pop file:///data/mirror

  or short form

      gsyncd.py :pop /data/mirror

2. `gsyncd :pop :yow`
3.

       gsyncd.py :pop ssh://example.com:/data/far_mirror

  or short form

       gsyncd.py :pop example.com:/data/far_mirror

4. `gsyncd.py :pop example.com::moz`

gsyncd has to be available on both sides; it's location on the remote side has to be specified
via the "--remote-gsyncd" option (or "remote-gsyncd" config file parameter). (This option can also be
used for setting options on the remote side, although the suggested mode of operation is to
set parameters like log file / pid file in the configuration file.)
