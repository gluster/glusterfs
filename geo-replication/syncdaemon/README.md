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
* glusterfs: with marker and changelog support (master & slave);
* FUSE: glusterfs fuse module with auxiliary gfid based access support

INSTALLATION
------------

As of now, the supported way of operation is running from the source directory or using the RPMs given.

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

By default (unless specified by the option `-c`), gsyncd looks for config file at _conf/gsyncd_template.conf_
in the source tree.

USAGE
-----

gsyncd is a utilitly for continuous mirroring, ie. it mirrors master to slave incrementally.
Assume we have a gluster volume _pop_ at localhost. We try to set up the mirroring for volume
_pop_ using gsyncd for gluster volume _moz_ on remote machine/cluster @ example.com. The
respective gsyncd invocations are (demoing some syntax sugaring):

`gsyncd.py :pop example.com::moz`

gsyncd has to be available on both sides; it's location on the remote side has to be specified
via the "--remote-gsyncd" option (or "remote-gsyncd" config file parameter). (This option can also be
used for setting options on the remote side, although the suggested mode of operation is to
set parameters like log file / pid file in the configuration file.)
