How to compile GlusterFS RPMs from git source, for RHEL/CentOS, and Fedora
--------------------------------------------------------------------------

Creating rpm's of GlusterFS from git source is fairly easy, once you
know the steps.

RPMS can be compiled on at least the following OS's:

-   Red Hat Enterprise Linux 5, 6 (& 7 when available)
-   CentOS 5, 6 (& 7 when available)
-   Fedora 16-20

Specific instructions for compiling are below. If you're using:

-   Fedora 16-20 - Follow the Fedora steps, then do all of the Common
    steps.
-   CentOS 5.x - Follow the CentOS 5.x steps, then do all of the Common
    steps
-   CentOS 6.x - Follow the CentOS 6.x steps, then do all of the Common
    steps.
-   RHEL 6.x - Follow the RHEL 6.x steps, then do all of the Common
    steps.

Note - these instructions have been explicitly tested on all of CentOS
5.10, RHEL 6.4, CentOS 6.4+, and Fedora 16-20. Other releases of
RHEL/CentOS and Fedora may work too, but haven't been tested. Please
update this page appropriately if you do so. :)

### Preparation steps for Fedora 16-20 (only)

​1. Install gcc, the python development headers, and python setuptools:

		$ sudo yum -y install gcc python-devel python-setuptools

​2. If you're compiling GlusterFS version 3.4, then install
python-swiftclient. Other GlusterFS versions don't need it:

		$ sudo easy_install simplejson python-swiftclient

Now follow through the **Common Steps** part below.

### Preparation steps for CentOS 5.x (only)

You'll need EPEL installed first and some CentOS specific packages. The
commands below will get that done for you. After that, follow through
the "Common steps" section.

​1. Install EPEL first:

		$ curl -OL http://download.fedoraproject.org/pub/epel/5/x86_64/epel-release-5-4.noarch.rpm
		$ sudo yum -y install epel-release-5-4.noarch.rpm --nogpgcheck

​2. Install the packages required only on CentOS 5.x:

		$ sudo yum -y install buildsys-macros gcc ncurses-devel python-ctypes python-sphinx10 \
		  redhat-rpm-config

Now follow through the **Common Steps** part below.

### Preparation steps for CentOS 6.x (only)

You'll need EPEL installed first and some CentOS specific packages. The
commands below will get that done for you. After that, follow through
the "Common steps" section.

​1. Install EPEL first:

		$ sudo yum -y install http://download.fedoraproject.org/pub/epel/6/x86_64/epel-release-6-8.noarch.rpm

​2. Install the packages required only on CentOS:

		$ sudo yum -y install python-webob1.0 python-paste-deploy1.5 python-sphinx10 redhat-rpm-config

Now follow through the **Common Steps** part below.

### Preparation steps for RHEL 6.x (only)

You'll need EPEL installed first and some RHEL specific packages. The 2
commands below will get that done for you. After that, follow through
the "Common steps" section.

​1. Install EPEL first:

		$ sudo yum -y install http://download.fedoraproject.org/pub/epel/6/x86_64/epel-release-6-8.noarch.rpm

​2. Install the packages required only on RHEL:

		$ sudo yum -y --enablerepo=rhel-6-server-optional-rpms install python-webob1.0 \
		  python-paste-deploy1.5 python-sphinx10 redhat-rpm-config

Now follow through the **Common Steps** part below.

### Common Steps

These steps are for both Fedora and RHEL/CentOS. At the end you'll have
the complete set of GlusterFS RPMs for your platform, ready to be
installed.

**NOTES for step 1 below:**

-   If you're on RHEL/CentOS 5.x and get a message about lvm2-devel not
    being available, it's ok. You can ignore it. :)
-   If you're on RHEL/CentOS 6.x and get any messages about
    python-eventlet, python-netifaces, python-sphinx and/or pyxattr not
    being available, it's ok. You can ignore them. :)

​1. Install the needed packages

		$ sudo yum -y --disablerepo=rhs* --enablerepo=*optional-rpms install git autoconf \
		  automake bison cmockery2-devel dos2unix flex fuse-devel glib2-devel libaio-devel \
		  libattr-devel libibverbs-devel librdmacm-devel libtool libxml2-devel lvm2-devel make \
		  openssl-devel pkgconfig pyliblzma python-devel python-eventlet python-netifaces \
		  python-paste-deploy python-simplejson python-sphinx python-webob pyxattr readline-devel \
		  rpm-build systemtap-sdt-devel tar libcmocka-devel

​2. Clone the GlusterFS git repository

		$ git clone git://git.gluster.org/glusterfs
		$ cd glusterfs

​3. Choose which branch to compile

If you want to compile the latest development code, you can skip this
step and go on to the next one.

If instead you want to compile the code for a specific release of
GlusterFS (such as v3.4), get the list of release names here:

		$ git branch -a | grep release
		  remotes/origin/release-2.0
		  remotes/origin/release-3.0
		  remotes/origin/release-3.1
		  remotes/origin/release-3.2
		  remotes/origin/release-3.3
		  remotes/origin/release-3.4
		  remotes/origin/release-3.5

Then switch to the correct release using the git "checkout" command, and
the name of the release after the "remotes/origin/" bit from the list
above:

		$ git checkout release-3.4

**NOTE -** The CentOS 5.x instructions have only been tested for the
master branch in GlusterFS git. It is unknown (yet) if they work for
branches older then release-3.5.

​4. Configure and compile GlusterFS

Now you're ready to compile Gluster:

		$ ./autogen.sh
		$ ./configure --enable-fusermount
		$ make dist

​5. Create the GlusterFS RPMs

		$ cd extras/LinuxRPM
		$ make glusterrpms

That should complete with no errors, leaving you with a directory
containing the RPMs.

		$ ls -l *rpm
		-rw-rw-r-- 1 jc jc 3966111 Mar  2 12:15 glusterfs-3git-1.el5.centos.src.rpm
		-rw-rw-r-- 1 jc jc 1548890 Mar  2 12:17 glusterfs-3git-1.el5.centos.x86_64.rpm
		-rw-rw-r-- 1 jc jc   66680 Mar  2 12:17 glusterfs-api-3git-1.el5.centos.x86_64.rpm
		-rw-rw-r-- 1 jc jc   20399 Mar  2 12:17 glusterfs-api-devel-3git-1.el5.centos.x86_64.rpm
		-rw-rw-r-- 1 jc jc  123806 Mar  2 12:17 glusterfs-cli-3git-1.el5.centos.x86_64.rpm
		-rw-rw-r-- 1 jc jc 7850357 Mar  2 12:17 glusterfs-debuginfo-3git-1.el5.centos.x86_64.rpm
		-rw-rw-r-- 1 jc jc  112677 Mar  2 12:17 glusterfs-devel-3git-1.el5.centos.x86_64.rpm
		-rw-rw-r-- 1 jc jc  100410 Mar  2 12:17 glusterfs-fuse-3git-1.el5.centos.x86_64.rpm
		-rw-rw-r-- 1 jc jc  187221 Mar  2 12:17 glusterfs-geo-replication-3git-1.el5.centos.x86_64.rpm
		-rw-rw-r-- 1 jc jc  299171 Mar  2 12:17 glusterfs-libs-3git-1.el5.centos.x86_64.rpm
		-rw-rw-r-- 1 jc jc   44943 Mar  2 12:17 glusterfs-rdma-3git-1.el5.centos.x86_64.rpm
		-rw-rw-r-- 1 jc jc  123065 Mar  2 12:17 glusterfs-regression-tests-3git-1.el5.centos.x86_64.rpm
		-rw-rw-r-- 1 jc jc   16224 Mar  2 12:17 glusterfs-resource-agents-3git-1.el5.centos.x86_64.rpm
		-rw-rw-r-- 1 jc jc  654043 Mar  2 12:17 glusterfs-server-3git-1.el5.centos.x86_64.rpm