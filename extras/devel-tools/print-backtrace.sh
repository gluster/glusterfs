#!/bin/bash
# sample unresolved backtrace lines picked up from a brick log that should go
# into a backtrace file eg. bt-file.txt:
# /usr/lib64/glusterfs/3.8.4/xlator/cluster/replicate.so(+0x3ec81)[0x7fe4bc271c81]
# /usr/lib64/glusterfs/3.8.4/xlator/cluster/replicate.so(+0x3eecd)[0x7fe4bc271ecd]
# /usr/lib64/glusterfs/3.8.4/xlator/cluster/replicate.so(+0x404cb)[0x7fe4bc2734cb]
# /usr/lib64/glusterfs/3.8.4/xlator/cluster/replicate.so(+0x3d2b6)[0x7fe4bc2702b6]
# /usr/lib64/glusterfs/3.8.4/xlator/cluster/replicate.so(+0x3d323)[0x7fe4bc270323]
#
# following is the output of the script for the above backtrace lines:
# /usr/lib64/glusterfs/3.8.4/xlator/cluster/replicate.so(+0x3ec81)[0x7fe4bc271c81] __afr_selfheal_data_finalize_source inlined at /usr/src/debug/glusterfs-3.8.4/xlators/cluster/afr/src/afr-self-heal-data.c:684 in __afr_selfheal_data_prepare /usr/src/debug/glusterfs-3.8.4/xlators/cluster/afr/src/afr-self-heal-data.c:603
# /usr/lib64/glusterfs/3.8.4/xlator/cluster/replicate.so(+0x3eecd)[0x7fe4bc271ecd] __afr_selfheal_data /usr/src/debug/glusterfs-3.8.4/xlators/cluster/afr/src/afr-self-heal-data.c:740
# /usr/lib64/glusterfs/3.8.4/xlator/cluster/replicate.so(+0x404cb)[0x7fe4bc2734cb] afr_selfheal_data /usr/src/debug/glusterfs-3.8.4/xlators/cluster/afr/src/afr-self-heal-data.c:883
# /usr/lib64/glusterfs/3.8.4/xlator/cluster/replicate.so(+0x3d2b6)[0x7fe4bc2702b6] afr_selfheal_do /usr/src/debug/glusterfs-3.8.4/xlators/cluster/afr/src/afr-self-heal-common.c:1968
# /usr/lib64/glusterfs/3.8.4/xlator/cluster/replicate.so(+0x3d323)[0x7fe4bc270323] afr_selfheal /usr/src/debug/glusterfs-3.8.4/xlators/cluster/afr/src/afr-self-heal-common.c:2015
#
# Usage with debuginfo RPM:
# print-backtrace.sh $HOME/Downloads/glusterfs-debuginfo-3.8.4-10.el7.x86_64.rpm bt-file.txt
#
# Usage with source install:
# print-packtrace.sh none bt-file.txt

function version_compare() { test $(echo $1|awk -F '.' '{print $1 $2 $3}') -gt $(echo $2|awk -F '.' '{print $1 $2 $3}'); }

function Usage()
{
        echo -e "Usage:\n\t$0 { none | <debuginfo-rpm> } <backtrace-file>"
        echo "none: implies we don't have a debuginfo rpm but want to resolve"
        echo "      against a source install which already has the debuginfo"
        echo "      NOTE: in this case you should have configured the build"
        echo "            with --enable-debug and the linker options should"
        echo "            have the option -rdynamic"
}

debuginfo_rpm=$1
backtrace_file=$2

if [ ! $debuginfo_rpm ] || [ ! $backtrace_file ]; then
        Usage
        exit 1
fi

if [ $debuginfo_rpm != "none" ] && [ ! -f $debuginfo_rpm ]; then
        echo "no such rpm file: $debuginfo_rpm"
        exit 1
fi

if [ ! -f $backtrace_file ]; then
        echo "no such backtrace file: $backtrace_file"
        exit 1
fi

if ! file $debuginfo_rpm | grep RPM >/dev/null 2>&1 ; then
        echo "file does not look like an rpm: $debuginfo_rpm"
        exit 1
fi

cpio_version=$(cpio --version|grep cpio|cut -f 2 -d ')'|sed -e 's/^[[:space:]]*//')
rpm_name=""
debuginfo_path=""
debuginfo_extension=""

if [ $debuginfo_rpm != "none" ]; then
        # extract the gluster debuginfo rpm to resolve the symbols against
        rpm_name=$(basename $debuginfo_rpm '.rpm')
        if [ -d $rpm_name ]; then
                echo "directory already exists: $rpm_name"
                echo "please remove/move it and reattempt"
                exit 1
        fi
        mkdir -p $rpm_name
        if version_compare $cpio_version "2.11"; then
                rpm2cpio $debuginfo_rpm | cpio --quiet --extract --make-directories --preserve-modification-time --directory=$rpm_name
                ret=$?
        else
                current_dir="$PWD"
                cd $rpm_name
                rpm2cpio $debuginfo_rpm | cpio --quiet --extract --make-directories --preserve-modification-time
                ret=$?
                cd $current_dir
        fi
        if [ $ret -eq 1 ]; then
                echo "failed to extract rpm $debuginfo_rpm to $PWD/$rpm_name directory"
                rm -rf $rpm_name
                exit 1
        fi
        debuginfo_path="$PWD/$rpm_name/usr/lib/debug"
        debuginfo_extension=".debug"
else
        debuginfo_path=""
        debuginfo_extension=""
fi

# NOTE: backtrace file should contain only the lines which need to be resolved
for bt in $(grep glusterfs $backtrace_file)
do
        libname=$(echo $bt | cut -f 1 -d '(')
        addr=$(echo $bt | cut -f 2 -d '(' | cut -f 1 -d ')')
        # only unresolved addresses start with a '+'
        if echo $addr | egrep '^\+' >/dev/null 2>&1 ; then
                newbt=( $(eu-addr2line --functions --exe=${debuginfo_path}${libname}${debuginfo_extension} $addr) )
                echo "$bt ${newbt[*]}"
        fi
done

# remove the temporary directory
if [ -d $rpm_name ]; then
        rm -rf $rpm_name
fi

