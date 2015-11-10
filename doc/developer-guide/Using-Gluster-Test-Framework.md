Description
-----------

The Gluster Test Framework, is a suite of scripts used for regression
testing of Gluster.

It runs well on RHEL and CentOS (possibly Fedora too, presently being
tested), and is automatically run against every patch submitted to
Gluster [for review](http://review.gluster.org).

The Gluster Test Framework is part of the main Gluster code base, living
under the "tests" subdirectory:

		http://git.gluster.org/?p=glusterfs.git;a=summary

WARNING
-------

Running the Gluster Test Framework deletes “/var/lib/glusterd/\*”.

**DO NOT run it on a server with any data.**

Preparation steps for Ubuntu 14.04 LTS
--------------------------------------

​1. \# apt-get install dbench git libacl1-dev mock nfs-common
nfs-kernel-server libtest-harness-perl libyajl-dev xfsprogs psmisc attr
acl lvm2 rpm

​2. \# apt-get install python-webob python-paste python-sphinx

​3. \# apt-get install autoconf automake bison dos2unix flex libfuse-dev
libaio-dev libibverbs-dev librdmacm-dev libtool libxml2-dev
libxml2-utils liblvm2-dev make libssl-dev pkg-config libpython-dev
python-eventlet python-netifaces python-simplejson python-pyxattr
libreadline-dev tar

​4) Install cmockery2 from github (https://github.com/lpabon/cmockery2)
and compile and make install as in Readme

5)

		sudo groupadd mock
		sudo useradd -g mock mock

​6) mkdir /var/run/gluster

**Note**: redhat-rpm-config package is not found in ubuntu

Preparation steps for CentOS 7 (only)
-------------------------------------

​1. Install EPEL:

		$ sudo yum install -y http://epel.mirror.net.in/epel/7/x86_64/e/epel-release-7-1.noarch.rpm

​2. Install the CentOS 7.x dependencies:

		$ sudo yum install -y --enablerepo=epel cmockery2-devel dbench git libacl-devel mock nfs-utils perl-Test-Harness yajl xfsprogs psmisc

		$ sudo yum install -y --enablerepo=epel python-webob1.0 python-paste-deploy1.5 python-sphinx10 redhat-rpm-config

==\> Despite below missing packages it worked for me

		No package python-webob1.0 available.
		No package python-paste-deploy1.5 available.
		No package python-sphinx10 available.

		$ sudo yum install -y --enablerepo=epel autoconf automake bison dos2unix flex fuse-devel libaio-devel libibverbs-devel \
		 librdmacm-devel libtool libxml2-devel lvm2-devel make openssl-devel pkgconfig \
		 python-devel python-eventlet python-netifaces python-paste-deploy \
		 python-simplejson python-sphinx python-webob pyxattr readline-devel rpm-build \
		 tar

​3. Create the mock user

		$ sudo useradd -g mock mock

Preparation steps for CentOS 6.3+ (only)
----------------------------------------

​1. Install EPEL:

		$ sudo yum install -y http://download.fedoraproject.org/pub/epel/6/x86_64/epel-release-6-8.noarch.rpm

​2. Install the CentOS 6.x dependencies:

		$ sudo yum install -y --enablerepo=epel cmockery2-devel dbench git libacl-devel mock nfs-utils perl-Test-Harness yajl xfsprogs
		$ sudo yum install -y --enablerepo=epel python-webob1.0 python-paste-deploy1.5 python-sphinx10 redhat-rpm-config
		$ sudo yum install -y --enablerepo=epel autoconf automake bison dos2unix flex fuse-devel libaio-devel libibverbs-devel \
		  librdmacm-devel libtool libxml2-devel lvm2-devel make openssl-devel pkgconfig \
		  python-devel python-eventlet python-netifaces python-paste-deploy \
		  python-simplejson python-sphinx python-webob pyxattr readline-devel rpm-build \
		  tar

​3. Create the mock user

		$ sudo useradd -g mock mock

Preparation steps for RHEL 6.3+ (only)
--------------------------------------

​1. Ensure you have the "Scalable Filesystem Support" group installed

This provides the xfsprogs package, which is required by the test
framework.

​2. Install EPEL:

		$ sudo yum install -y http://download.fedoraproject.org/pub/epel/6/x86_64/epel-release-6-8.noarch.rpm

​3. Install the CentOS 6.x dependencies:

		$ sudo yum install -y --enablerepo=epel cmockery2-devel dbench git libacl-devel mock nfs-utils yajl perl-Test-Harness
		$ sudo yum install -y --enablerepo=rhel-6-server-optional-rpms python-webob1.0 python-paste-deploy1.5 python-sphinx10 redhat-rpm-config
		$ sudo yum install -y --disablerepo=rhs* --enablerepo=*optional-rpms autoconf \
		  automake bison dos2unix flex fuse-devel libaio-devel libibverbs-devel \
		  librdmacm-devel libtool libxml2-devel lvm2-devel make openssl-devel pkgconfig \
		  python-devel python-eventlet python-netifaces python-paste-deploy \
		  python-simplejson python-sphinx python-webob pyxattr readline-devel rpm-build \
		  tar

​4. Create the mock user

		$ sudo useradd -g mock mock

Preparation steps for Fedora 16-19 (only)
-----------------------------------------

**Still in development**

​1. Install the Fedora dependencies:

		$ sudo yum install -y attr cmockery2-devel dbench git mock nfs-utils perl-Test-Harness psmisc xfsprogs
		$ sudo yum install -y python-webob1.0 python-paste-deploy1.5 python-sphinx10 redhat-rpm-config
		$ sudo yum install -y autoconf automake bison dos2unix flex fuse-devel libaio-devel libibverbs-devel \
		  librdmacm-devel libtool libxml2-devel lvm2-devel make openssl-devel pkgconfig \
		  python-devel python-eventlet python-netifaces python-paste-deploy \
		  python-simplejson python-sphinx python-webob pyxattr readline-devel rpm-build \
		  tar

​3. Create the mock user

		$ sudo useradd -g mock mock

Common steps
------------

​1. Ensure DNS for your server is working

The Gluster Test Framework fails miserably if the full domain name for
your server doesn't resolve back to itself.

If you don't have a working DNS infrastructure in place, adding an entry
for your server to its /etc/hosts file will work.

​2. Install the version of Gluster you are testing

Either install an existing set of rpms:

		$ sudo yum install [your gluster rpms here]

Or compile your own ones (fairly easy):

	http://www.gluster.org/community/documentation/index.php/CompilingRPMS

​3. Clone the GlusterFS git repository

		$ git clone git://git.gluster.org/glusterfs
		$ cd glusterfs

Ensure mock can access the directory
------------------------------------

Some tests run as the user "mock". If the mock user can't access the
tests subdirectory directory, these tests fail. (rpm.t is one such test)

This is a known gotcha when the git repo is cloned to your home
directory. Home directories generally don't have world readable
permissions. You can fix this by adjusting your home directory
permissions, or placing the git repo somewhere else (with access for the
mock user).

Running the tests
-----------------

The tests need to run as root, so they can mount volumes and manage
gluster processes as needed.

It's also best to run them directly as the root user, instead of through
sudo. Strange things sporadicly happen (for me) when using the full test
framework through sudo, that haven't happened (yet) when running
directly as root. Hangs in dbench particularly, which are part of at
least one test.

		# ./run-tests.sh

The test framework takes just over 45 minutes to run in a VM here (4
cpu's assigned, 8GB ram, SSD storage). It may take significantly more or
less time for you, depending on the hardware and software you're using.

Showing debug information
-------------------------

To display verbose information while the tests are running, set the
DEBUG environment variable to 1 prior to running the tests.

		# DEBUG=1 ./run-tests.sh

Log files
---------

Verbose output from the rpm.t test goes into "rpmbuild-mock.log",
located in the same directory the test is run from.

Reporting bugs
--------------

If you hit a bug when running the test framework, **please** create a
bug report for it on Bugzilla so it gets fixed:

	https://bugzilla.redhat.com/enter_bug.cgi?product=GlusterFS&component=tests

Creating your own tests
-----------------------

The test scripts are written in bash, with their filenames ending in .t
instead of .sh.

When creating your own test scripts, create them in an appropriate
subdirectory under "tests" (eg "bugs" or "features") and use descriptive
names like "bug-XXXXXXX-checking-feature-X.t"

Also include the "include.rc" file, which defines the test types and
host/brick/volume defaults:

		. $(dirname $0)/../include.rc

There are 5 test types available at present, but feel free to add more
if you need something that doesn't yet exist. The test types are
explained in more detail below.

Also essential is the "cleanup" command, which removes any existing
Gluster configuration (**without backing it up**), and also kills any
running gluster processes.

There is a basic test template you can copy, named bug-000000.t in the
bugs subdirectory:

		$ cp bugs/bug-000000.t somedir/descriptive-name.t

### TEST

-   Example of usage in basic/volume.t

### TEST\_IN\_LOOP

-   Example of usage in basic/rpm.t

### EXPECT

-   Example of usage in basic/volume.t

### EXPECT\_WITHIN

-   Example of usage in basic/volume-status.t

### EXPECT\_KEYWORD

-   Defined in include.rc, but seems to be unused?
