#!/bin/bash

syscalls=$'access\nchmod\nchown\nclose\nclosedir\ncreat64\n\
fallocate64\nfchmod\nfchown\nfdatasync\nfgetxattr\nflistxattr\n\
fremovexattr\nfsetxattr\nfsync\nftruncate64\n__fxstat64\n\
__fxstatat64\nlchown\nlgetxattr\nlinkat\nllistxattr\nlremovexattr\n\
lseek64\nlsetxattr\n__lxstat64\nmkdir\nmkdirat\nopenat64\nopendir\n\
pread64\npwrite64\npreadv64\npwritev64\nread\nreaddir64\nreadlink\n\
readv\nrename\nrmdir\n statvfs64\nsymlink\n\truncate64\nunlink\n\
utimeswrite\nwritev\n\__xmknod\n__xstat64'

syscalls32=$'creat\nfallocate\nftruncate\n__fxstat\n__fxstatat\n\
lseek\n__lxstat\nopenat\nreaddir\nstatvfs\ntruncate\nstat\n\
preadv\npwritev\npread\npwrite'

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
                "preadv64") sym="preadv";;
                "pwritev64") sym="pwritev";;
                "pread64") sym="pread";;
                "pwrite64") sym="pwrite";;
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
