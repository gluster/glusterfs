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
    vagrant -v;

    if [ $? -ne 0 ]; then
        echo "Aborting"
        echo "Vagrant not found. Please install Vagrant and try again."
        exit 1
    else
        echo "Found Vagrant, continuing...."
        echo
    fi
}

function ansible_check()
{
    ansible --version;

    if [ $? -ne 0 ]; then
        echo "Aborting"
        echo "Ansible not found. Please install Ansible and try again."
        exit 1
    else
        echo "Found Ansible, continuing...."
        echo
    fi
}

force_location

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
cd tests/vagrant/$BRANCHNAME
echo "Working directory is $PWD"
echo
echo

echo "Doing vagrant up...."
vagrant up || { echo "vagrant up failed, exiting...."; exit 1; }
echo
echo


echo "Vagrant up successfull"
echo
echo


vagrant ssh-config > ssh_config

echo "Copying source code from host machine to VM"
rsync -az -e "ssh -F ssh_config" "../../../." vagrant-testVM:/home/vagrant/glusterfs
#scp -r -F ssh_config "./../../../." vagrant-testVM:/home/vagrant/glusterfs
echo "Copied."
echo
echo

vagrant ssh -c 'cd /home/vagrant/glusterfs ; ./autogen.sh' -- -t
echo
echo

vagrant ssh -c 'cd /home/vagrant/glusterfs ; \
        CFLAGS="-g -O0 -Werror -Wall -Wno-error=cpp -Wno-error=maybe-uninitialized" \
        ./configure \
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
echo
echo


vagrant ssh -c 'cd /home/vagrant/glusterfs; sudo make install' -- -t
echo
echo

vagrant ssh -c 'cd /home/vagrant/glusterfs; sudo ./run-tests.sh' -- -t
echo
echo
