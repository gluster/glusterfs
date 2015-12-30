#!/bin/bash

###############################################################################
# TODO: Provide an option parser; may be getopts.                             #
# TODO: Allow subset of tests to be executed when VM starts.                  #
# TODO: Provide option to destroy the VM.                                     #
###############################################################################

function force_location()
{
    current_dir=$(dirname $0);

    if [ ! -f ${current_dir}/tests/vagrant/vagrant-template/Vagrantfile ]; then
        echo "Aborting."
        echo
        echo "The tests/vagrant subdirectory seems to be missing."
        echo
        echo "Please correct the problem and try again."
        echo
        exit 1
    fi
}

function vagrant_check()
{
    vagrant -v >/dev/null 2>&1;

    if [ $? -ne 0 ]; then
        echo "Aborting"
        echo "Vagrant not found. Please install Vagrant and try again."
        echo "On Fedora, run "dnf install vagrant vagrant-libvirt" "
        exit 1
    else
        echo "Found Vagrant, continuing...."
        echo
    fi
}

function ansible_check()
{
    ansible --version  >/dev/null  2>&1 ;

    if [ $? -ne 0 ]; then
        echo "Aborting"
        echo "Ansible not found. Please install Ansible and try again."
        echo "On Fedora, run "dnf install ansible" "
        exit 1
    else
        echo "Found Ansible, continuing...."
        echo
    fi
}

ORIGIN_DIR=$PWD

echo "Checking current dir...."
force_location
echo
echo

echo "Testing for Vagrant...."
vagrant_check
echo
echo

echo "Testing for Ansible...."
ansible_check
echo
echo

BRANCHNAME=`git rev-parse --abbrev-ref HEAD`
echo "Copying tests/vagrant/vagrant-template dir to tests/vagrant/$BRANCHNAME"
mkdir -p tests/vagrant/$BRANCHNAME
cp -R tests/vagrant/vagrant-template/* tests/vagrant/$BRANCHNAME
echo "Change dir to vagrant dir: tests/vagrant/$BRANCHNAME"
echo "Vagrant directory is tests/vagrant/$BRANCHNAME"
echo
echo




echo "Doing vagrant up...."
cd tests/vagrant/$BRANCHNAME
vagrant up
if [ $? -eq 0 ]
then
        echo "Vagrant up successful"
        cd $ORIGIN_DIR
else
        echo "Vagrant up failed, exiting....";
        cd $ORIGIN_DIR
        exit 1
fi
echo
echo



echo "Copying source code from host machine to VM"
cd tests/vagrant/$BRANCHNAME
vagrant ssh-config > ssh_config
rsync -az -e "ssh -F ssh_config" --rsync-path="sudo rsync" "../../../." vagrant-testVM:/home/vagrant/glusterfs
if [ $? -eq 0 ]
then
        echo "Copied."
        cd $ORIGIN_DIR
else
        echo "Copy failed, exiting...."
        cd $ORIGIN_DIR
        exit 1
fi
echo
echo


cd tests/vagrant/$BRANCHNAME
vagrant ssh -c 'cd /home/vagrant/glusterfs ; sudo make clean' -- -t
cd $ORIGIN_DIR
echo
echo

cd tests/vagrant/$BRANCHNAME
vagrant ssh -c 'cd /home/vagrant/glusterfs ; sudo ./autogen.sh' -- -t
if [ $? -ne 0 ]
then
        echo "autogen failed, exiting...."
        cd $ORIGIN_DIR
        exit 1
fi
cd $ORIGIN_DIR
echo
echo

cd tests/vagrant/$BRANCHNAME
vagrant ssh -c 'cd /home/vagrant/glusterfs ; \
        CFLAGS="-g -O0 -Werror -Wall -Wno-error=cpp -Wno-error=maybe-uninitialized" \
        sudo ./configure \
        --prefix=/usr \
        --exec-prefix=/usr \
        --bindir=/usr/bin \
        --sbindir=/usr/sbin \
        --sysconfdir=/etc \
        --datadir=/usr/share \
        --includedir=/usr/include \
        --libdir=/usr/lib64 \
        --libexecdir=/usr/libexec \
        --localstatedir=/var \
        --sharedstatedir=/var/lib \
        --mandir=/usr/share/man \
        --infodir=/usr/share/info \
        --libdir=/usr/lib64 \
        --enable-debug' -- -t
if [ $? -ne 0 ]
then
        echo "configure failed, exiting...."
        cd $ORIGIN_DIR
        exit 1
fi
cd $ORIGIN_DIR
echo
echo

cd tests/vagrant/$BRANCHNAME
vagrant ssh -c 'cd /home/vagrant/glusterfs; sudo make install' -- -t
if [ $? -ne 0 ]
then
        echo "make failed, exiting...."
        cd $ORIGIN_DIR
        exit 1
fi
cd $ORIGIN_DIR
echo
echo

cd tests/vagrant/$BRANCHNAME
vagrant ssh -c 'cd /home/vagrant/glusterfs; sudo ./run-tests.sh' -- -t
cd $ORIGIN_DIR
echo
echo
