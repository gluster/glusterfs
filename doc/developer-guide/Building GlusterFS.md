This page describes how to build and install GlusterFS.

Build Requirements
------------------

The following packages are required for building GlusterFS,

-   GNU Autotools
    -   Automake
    -   Autoconf
    -   Libtool
-   lex (generally flex)
-   GNU Bison
-   OpenSSL
-   libxml2
-   Python 2.x
-   libaio
-   libibverbs
-   librdmacm
-   readline
-   lvm2
-   glib2
-   liburcu
-   cmocka
-   libacl
-   sqlite

### Fedora

The following yum command installs all the build requirements for
Fedora,

		# yum install automake autoconf libtool flex bison openssl-devel libxml2-devel python-devel libaio-devel libibverbs-devel librdmacm-devel readline-devel lvm2-devel glib2-devel userspace-rcu-devel libcmocka-devel libacl-devel sqlite-devel

### Ubuntu

The following apt-get command will install all the build requirements on
Ubuntu,

		$ sudo apt-get install make automake autoconf libtool flex bison pkg-config libssl-dev libxml2-dev python-dev libaio-dev libibverbs-dev librdmacm-dev libreadline-dev liblvm2-dev libglib2.0-dev liburcu-dev libcmocka-dev libsqlite3-dev libacl1-dev

Building from Source
--------------------

This section describes how to build GlusterFS from source. It is assumed
you have a copy of the GlusterFS source (either from a released tarball
or a git clone). All the commands below are to be run with the source
directory as the working directory.

### Configuring for building

Run the below commands once for configuring and setting up the build
process.

Run autogen to generate the configure script.

		$ ./autogen.sh

Once autogen completes successfully a configure script is generated. Run
the configure script to generate the makefiles.

		$ ./configure

If the above build requirements have been installed, running the
configure script should give the below configure summary,

		GlusterFS configure summary
		===========================
		FUSE client          : yes
		Infiniband verbs     : yes
		epoll IO multiplex   : yes
		argp-standalone      : no
		fusermount           : yes
		readline             : yes
		georeplication       : yes
		Linux-AIO            : yes
		Enable Debug         : no
		Block Device xlator  : yes
		glupy                : yes
		Use syslog           : yes
		XML output           : yes
		QEMU Block formats   : yes
		Encryption xlator    : yes

During development it is good to enable a debug build. To do this run
configure with a '--enable-debug' flag.

		$ ./configure --enable-debug

Further configuration flags can be found by running configure with a
'--help' flag,

		$ ./configure --help

### Building

Once configured, GlusterFS can be built with a simple make command.

		$ make

To speed up the build process on a multicore machine, add a '-jN' flag,
where N is the number of parallel jobs.

### Installing

Run 'make install' to install GlusterFS. By default, GlusterFS will be
installed into '/usr/local' prefix. To change the install prefix, give
the appropriate option to configure. If installing into the default
prefix, you might need to use 'sudo' or 'su -c' to install.

		$ sudo make install

### Running GlusterFS

GlusterFS can be only run as root, so the following commands will need
to be run as root. If you've installed into the default '/usr/local'
prefix, add '/usr/local/sbin' and '/usr/local/bin' to your PATH before
running the below commands.

A source install will generally not install any init scripts. So you
will need to start glusterd manually. To manually start glusterd just
run,

		# glusterd

This will start glusterd and fork it into the background as a daemon
process. You now run 'gluster' commands and make use of GlusterFS.

Building packages
-----------------

### Building RPMs

Building RPMs is really simple. On a RPM based system, for eg. Fedora,
get the source and do the configuration steps as shown in the 'Building
from Source' section. After the configuration step, run the following
steps to build RPMs,

		$ cd extras/LinuxRPM
		$ make glusterrpms

This will create rpms from the source in 'extras/LinuxRPM'. *(Note: You
will need to install the rpmbuild requirements including rpmbuild and
mock)*

A more detailed description for building RPMs can be found at
[CompilingRPMS](./Compiling RPMS.md).
