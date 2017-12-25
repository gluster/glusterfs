#!/bin/bash

###############################################################################
# TODO: Support other OSes.                                                   #
###############################################################################

ORIGIN_DIR=$PWD
autostart="no"
destroy_after_test="no"
os="fedora"
destroy_now="no"
run_tests_args=""
redirect=">/dev/null 2>&1"
ssh="no"
custom_cflags=""


pushd () {
    command pushd "$@" >/dev/null
}

popd () {
    command popd "$@" >/dev/null
}

usage() {
    echo "Usage: $0 [...]"
    echo ''
    echo 'The options that this script accepts are:'
    echo ''
    echo '-a, --autostart        configure the testVM to autostart on boot'
    echo '--destroy-now          cleanup the testVM'
    echo '--destroy-after-test   cleanup once the tests finishes'
    echo '-h, --help             show this help text'
    echo '--os=<flavor>          select the OS for the testVM (fedora, centos6)'
    echo '--ssh                  ssh into the testVM'
    echo '--verbose              show what commands in the testVM are executed'
    echo ''
}

function parse_args () {
    args=`getopt \
              --options ah \
              --long autostart,os:,destroy-now,destroy-after-test,verbose,ssh,help \
              -n 'run-tests-in-vagrant.sh' \
              --  "$@"`
    eval set -- "$args"
    while true; do
        case "$1" in
            -a|--autostart) autostart="yes"; shift ;;
            --destroy-after-test) destroy_after_test="yes"; shift ;;
            --destroy-now)  destroy_now="yes"; shift ;;
            -h|--help) usage ; exit 0 ;;
            --ssh)  sshvm="yes"; shift ;;
            --os)
                case "$2" in
                    "") shift 2 ;;
                     *) os="$2" ; shift 2 ;;
                esac ;;
            --verbose)  redirect=""; shift ;;
            --) shift ; break ;;
            *) echo "Internal error!" ; exit 1;;
        esac
    done
    run_tests_args="$@"
}

function force_location()
{
    current_dir=$(dirname $0);

    if [ ! -f ${current_dir}/tests/vagrant/vagrant-template-fedora/Vagrantfile ]; then
        echo "Aborting."
        echo "The tests/vagrant subdirectory seems to be missing."
        echo "Please correct the problem and try again."
        exit 1
    fi
}

function vagrant_check()
{
    vagrant -v >/dev/null  2>&1;

    if [ $? -ne 0 ]; then
        echo "Aborting"
        echo "Vagrant not found. Please install Vagrant and try again."
        echo "On Fedora, run "dnf install vagrant vagrant-libvirt" "
        exit 1
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
    fi
}

function set_branchname_from_git_branch()
{
    BRANCHNAME=`git rev-parse --abbrev-ref HEAD`
    if [ $? -ne 0 ]; then
        echo "Could not get branch name from git, will exit"
        exit 1
    fi
}


function destroy_vm_and_exit()
{
    echo "!!!!!!!!!!!!!!!!!!!!!!!!!!!!CAUTION!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
    echo "This will destroy VM and delete tests/vagrant/${BRANCHNAME} dir"
    echo
    while true; do
        read -p "Do you want to continue?" yn
        case $yn in
            [Yy]* ) break;;
            * ) echo "Did not get an yes, exiting."; exit 1 ;;
        esac
    done
    if [ -d "tests/vagrant/${BRANCHNAME}" ]; then
        pushd "tests/vagrant/${BRANCHNAME}"
        eval vagrant destroy $redirect
        popd
        rm -rf "tests/vagrant/${BRANCHNAME}"
        exit 0
    else
        echo "Could not find vagrant dir for corresponding git branch, exiting"
        exit 1
    fi
}


function create_vagrant_dir()
{
    mkdir -p tests/vagrant/$BRANCHNAME
    if [ -d "tests/vagrant/vagrant-template-${os}" ]; then
        echo "Copying tests/vagrant/vagrant-template-${os} dir to tests/vagrant/${BRANCHNAME} ...."
        cp -R tests/vagrant/vagrant-template-${os}/* tests/vagrant/$BRANCHNAME
    else
        echo "Could not find template files for requested os $os, exiting"
        exit 1
    fi
}


function start_vm()
{
    echo "Doing vagrant up...."
    pushd "tests/vagrant/${BRANCHNAME}"
    eval vagrant up $redirect
    if [ $? -eq 0 ]
    then
            popd
    else
            echo "Vagrant up failed, exiting....";
            popd
            exit 1
    fi
}

function set_vm_attributes()
{
    if [ "x$autostart" == "xyes" ] ; then
        virsh autostart ${BRANCHNAME}_vagrant-testVM
    fi
}

function copy_source_code()
{
    echo "Copying source code from host machine to VM...."
    pushd "tests/vagrant/${BRANCHNAME}"
    vagrant ssh-config > ssh_config
    rsync -az -e "ssh -F ssh_config" --rsync-path="sudo rsync" "$ORIGIN_DIR/." vagrant-testVM:/home/vagrant/glusterfs
    if [ $? -eq 0 ]
    then
            popd
    else
            echo "Copy failed, exiting...."
            popd
            exit 1
    fi
}

function compile_gluster()
{
    echo "Source compile and install Gluster...."
    pushd "tests/vagrant/${BRANCHNAME}"
    vagrant ssh -c "cd /home/vagrant/glusterfs ; sudo make clean $redirect" -- -t
    vagrant ssh -c "cd /home/vagrant/glusterfs ; sudo ./autogen.sh $redirect" -- -t
    if [ $? -ne 0 ]
    then
            echo "autogen failed, exiting...."
            popd
            exit 1
    fi

    # GCC on fedora complains about uninitialized variables and
    # GCC on centos6 does not under don't warn on uninitialized variables flag.
    if [ "x$os" == "fedora" ] ; then
            custom_cflags="CFLAGS='-g -O0 -Werror -Wall -Wno-error=cpp -Wno-error=maybe-uninitialized'"
    else
            custom_cflags="CFLAGS='-g -O0 -Werror -Wall'"
    fi


    custom_cflags=
    vagrant ssh -c "cd /home/vagrant/glusterfs ; \
            sudo \
            $custom_cflags \
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
            --enable-gnfs \
            --enable-debug $redirect" -- -t
    if [ $? -ne 0 ]
    then
            echo "configure failed, exiting...."
            popd
            exit 1
    fi
    vagrant ssh -c "cd /home/vagrant/glusterfs; sudo make -j install $redirect" -- -t
    if [ $? -ne 0 ]
    then
            echo "make failed, exiting...."
            popd
            exit 1
    fi
    popd
}

function run_tests()
{
    pushd "tests/vagrant/${BRANCHNAME}"
    vagrant ssh -c "cd /home/vagrant/glusterfs; sudo ./run-tests.sh $run_tests_args" -- -t
    popd
}

function ssh_into_vm_using_exec()
{
    pushd "tests/vagrant/${BRANCHNAME}"
    exec vagrant ssh
    popd
}

echo
parse_args "$@"

# Check environment for dependencies
force_location
vagrant_check
ansible_check

# We have one vm per git branch, query git branch
set_branchname_from_git_branch

if [ "x$destroy_now" == "xyes" ] ; then
    destroy_vm_and_exit
fi

if [ "x$sshvm" == "xyes" ] ; then
    ssh_into_vm_using_exec
fi


create_vagrant_dir
start_vm
set_vm_attributes



copy_source_code
compile_gluster
run_tests

if [ "x$destroy_after_test" == "xyes" ] ; then
    destroy_vm_and_exit
fi
