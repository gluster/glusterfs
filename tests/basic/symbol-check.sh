#!/bin/bash

syscalls=$'access\nchmod\nchown\nclose\nclosedir\ncreat64\n\
fallocate64\nfchmod\nfchown\nfdatasync\nfgetxattr\nflistxattr\n\
fremovexattr\nfsetxattr\nfsync\nftruncate64\n__fxstat64\n\
__fxstatat64\nlchown\nlgetxattr\nlinkat\nllistxattr\nlremovexattr\n\
lseek64\nlsetxattr\n__lxstat64\nmkdir\nmkdirat\nopenat64\nopendir\n\
read\nreaddir64\nreadlink\nreadv\nrename\nrmdir\nstatvfs64\nsymlink\n\
truncate64\nunlink\nutimeswrite\nwritev\n__xmknod\n__xstat64'

syscalls32=$'creat\n\fallocate\n\ftruncate\n\__fxstat\n\__fxstatat\n\
lseek\n\__lxstat\n\openat\n\readdir\n\statvfs\n\truncate\n\stat'

exclude_files=$'/libglusterfs/src/.libs/libglusterfs_la-syscall.o\n\
/libglusterfs/src/.libs/libglusterfs_la-gen_uuid.o\n\
/contrib/fuse-util/fusermount.o\n\
/contrib/fuse-util/mount_util.o\n\
/contrib/fuse-util/mount-common.o\n\
/xlators/mount/fuse/src/.libs/mount.o\n\
/xlators/mount/fuse/src/.libs/mount-common.o\n\
/xlators/features/qemu-block/src/.libs/block.o\n\
/xlators/features/qemu-block/src/.libs/cutils.o\n\
/xlators/features/qemu-block/src/.libs/oslib-posix.o'

function main()
{
    for exclude_file in ${exclude_files}; do
        if [[ ${1} = *${exclude_file} ]]; then
            exit 0
        fi
    done

    local retval=0
    local t=$(nm ${1} | grep " U " | sed -e "s/  //g" -e "s/ U //g")

    for symy in ${t}; do

        for symx in ${syscalls}; do

            if [[ ${symx} = ${symy} ]]; then

                case ${symx} in
                "creat64") sym="creat";;
                "fallocate64") sym="fallocate";;
                "ftruncate64") sym="ftruncate";;
                "lseek64") sym="lseek";;
                "openat64") sym="openat";;
                "readdir64") sym="readdir";;
                "truncate64") sym="truncate";;
                "__statvfs64") sym="statvfs";;
                "__fxstat64") sym="fstat";;
                "__fxstatat64") sym="fstatat";;
                "__lxstat64") sym="lstat";;
                "__xmknod") sym="mknod";;
                "__xstat64") sym="stat";;
                *) sym=${symx};;
                esac

	        echo "${1} should call sys_${sym}, not ${sym}" >&2
                retval=1
	    fi

        done

        for symx in ${syscalls32}; do

            if [[ ${symx} = ${symy} ]]; then

                echo "${1} was not compiled with -D_FILE_OFFSET_BITS=64" >&2
                retval=1
            fi
        done
    done

    if [ ${retval} = 1 ]; then
        touch ./.symbol-check-errors
    fi
    exit ${retval}
}

main "$@"
