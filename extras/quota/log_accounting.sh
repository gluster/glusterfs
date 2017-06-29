#!/usr/bin/bash
# The script does an accounting of all directories using command 'du' and
# using gluster. We can then compare the two to identify accounting mismatch
# THere can be minor mismatch because gluster only accounts for the size of
# files. Direcotries can take up upto 4kB space on FS per directory. THis
# size is accounted by du and not by gluster. However the difference would
# not be significant.

mountpoint=$1
volname=$2

usage ()
{
    echo >&2 "usage: $0 <mountpoint> <volume name>"
    exit
}

[ $# -lt 2 ] && usage

cd $mountpoint
du -h | head -n -1 | tr -d '.' |awk  '{ for (i = 2; i <= NF; i++) { printf("%s ", $i);}  print "" }' > /tmp/gluster_quota_1
cat /tmp/gluster_quota_1 | sed 's/ $//' | sed 's/ /\\ /g' | sed 's/(/\\(/g' | sed 's/)/\\)/g' |xargs gluster v quota $volname list > /tmp/gluster_quota_2
du -h | head -n -1 |awk  '{ for (i = 2; i <= NF; i++) { printf("%s %s", $i, $1);}  print "" }' | tr -d '.' >  /tmp/gluster_quota_3
cat /tmp/gluster_quota_2 /tmp/gluster_quota_3 | sort > /tmp/gluster_quota_4
find . -type d > /tmp/gluster_quota_5
tar -cvf /tmp/gluster_quota_files.tar /tmp/gluster_quota_*
