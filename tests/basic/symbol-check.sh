#!/bin/bash

set -eEu

declare -A syscalls
declare -A syscalls32
declare -A glibccalls
declare -A exclude_files

syscalls[access]="access"
syscalls[chmod]="chmod"
syscalls[chown]="chown"
syscalls[close]="close"
syscalls[closedir]="closedir"
syscalls[creat64]="creat"
syscalls[fallocate64]="fallocate"
syscalls[fchmod]="fchmod"
syscalls[fchown]="fchown"
syscalls[fdatasync]="fdatasync"
syscalls[fgetxattr]="fgetxattr"
syscalls[flistxattr]="flistxattr"
syscalls[fremovexattr]="fremovexattr"
syscalls[fsetxattr]="fsetxattr"
syscalls[fsync]="fsync"
syscalls[ftruncate64]="ftruncate"
syscalls[__fxstat64]="fstat"
syscalls[__fxstatat64]="fstatat"
syscalls[lchown]="lchown"
syscalls[lgetxattr]="lgetxattr"
syscalls[linkat]="linkat"
syscalls[llistxattr]="llistxattr"
syscalls[lremovexattr]="lremovexattr"
syscalls[lseek64]="lseek"
syscalls[lsetxattr]="lsetxattr"
syscalls[__lxstat64]="lstat"
syscalls[mkdir]="mkdir"
syscalls[mkdirat]="mkdirat"
syscalls[openat64]="openat"
syscalls[opendir]="opendir"
syscalls[pread64]="pread"
syscalls[pwrite64]="pwrite"
syscalls[preadv64]="preadv"
syscalls[pwritev64]="pwritev"
syscalls[read]="read"
syscalls[readdir64]="readdir"
syscalls[readlink]="readlink"
syscalls[readv]="readv"
syscalls[rename]="rename"
syscalls[rmdir]="rmdir"
syscalls[statvfs64]="statvfs"
syscalls[symlink]="symlink"
syscalls[truncate64]="truncate"
syscalls[unlink]="unlink"
syscalls[utimeswrite]="utimeswrite"
syscalls[writev]="writev"
syscalls[__xmknod]="mknod"
syscalls[__xstat64]="stat"

syscalls32[creat]="creat"
syscalls32[fallocate]="fallocate"
syscalls32[ftruncate]="ftruncate"
syscalls32[__fxstat]="stat"
syscalls32[__fxstatat]="statat"
syscalls32[lseek]="lseek"
syscalls32[__lxstat]="stat"
syscalls32[openat]="openat"
syscalls32[readdir]="readdir"
syscalls32[statvfs]="statvfs"
syscalls32[truncate]="truncate"
syscalls32[stat]="stat"
syscalls32[preadv]="preadv"
syscalls32[pwritev]="pwritev"
syscalls32[pread]="pread"
syscalls32[pwrite]="pwrite"

glibccalls[tmpfile]="tmpfile"

exclude_files[./libglusterfs/src/.libs/libglusterfs_la-syscall.o]=1
exclude_files[./libglusterfs/src/.libs/libglusterfs_la-gen_uuid.o]=1
exclude_files[./contrib/fuse-lib/mount-common.o]=1
exclude_files[./contrib/fuse-lib/.libs/mount.o]=1
exclude_files[./contrib/fuse-lib/.libs/mount-common.o]=1
exclude_files[./contrib/fuse-util/fusermount.o]=1
exclude_files[./contrib/fuse-util/mount_util.o]=1
exclude_files[./contrib/fuse-util/mount-common.o]=1
exclude_files[./xlators/mount/fuse/src/.libs/mount.o]=1
exclude_files[./xlators/mount/fuse/src/.libs/mount-common.o]=1
exclude_files[./xlators/features/qemu-block/src/.libs/block.o]=1
exclude_files[./xlators/features/qemu-block/src/.libs/cutils.o]=1
exclude_files[./xlators/features/qemu-block/src/.libs/oslib-posix.o]=1

function main()
{
    local retval="0"

    if [[ -n "${exclude_files[${1}]-}" ]]; then
        return 0
    fi

    for symy in $(nm -pu "${1}" | sed 's/^.*\sU\s\+//'); do
        sym="${syscalls[${symy}]-}"
        if [[ -n "${sym}" ]]; then
            echo "${1} should call sys_${sym}, not ${sym}" >&2
            retval="1"
        fi

        sym="${syscalls32[${symy}]-}"
        if [[ -n "${sym}" ]]; then
            echo "${1} was not compiled with -D_FILE_OFFSET_BITS=64" >&2
            retval="1"
        fi

        symy_glibc="${symy%@@GLIBC*}"
        # Eliminate false positives, check if we have a GLIBC symbol in 'y'
        if [[ ${symy} != "${symy_glibc}" ]]; then
            alt="${glibccalls[${symy_glibc}]-}"
            if [[ -n "${alt}" ]]; then
                if [[ ${alt} = "none" ]]; then
                    echo "${1} should not call ${symy_glibc}" >&2
                else
                    echo "${1} should use ${alt} instead of ${symy_glibc}" >&2
                fi
                retval="1"
            fi
        fi
    done

    return ${retval}
}

base="$(realpath "${1}")"
cd "${base}"
find -name "*.o" |
    while read path; do
        if ! main "${path}"; then
            touch ./.symbol-check-errors
        fi
    done

