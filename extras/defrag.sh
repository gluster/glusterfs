#!/bin/sh

# This script gets called from 'scale-n-defrag.sh' script.
# Don't run this stand alone.
# 
#

set -e

CP="cp"
MV="mv"

scan_dir()
{
    path=$1;
    find "$path" -type f -perm +01000 -exec $0 '{}' \; 
}

rsync_filename()
{
    path=$1
    dir=$(dirname "$path");
    file=$(basename "$path");

    echo "$dir/.$file.zr$$";
}

relocate_file()
{
    path=$1;
    tmp_path=$(rsync_filename "$path");

    pre_mtime=$(stat -c '%Y' "$path");
    $CP -a "$path" "$tmp_path";
    post_mtime=$(stat -c '%Y' "$path");

    if [ $pre_mtime = $post_mtime ]; then
	chmod -t "$tmp_path";
	$MV "$tmp_path" "$path";
	echo "file '$path' relocated" 
    else
	echo "file '$path' modified during defrag. skipping"
	rm -f "$tmp_path";
    fi
}

main()
{
    path="$1";

    if [ -d "$path" ]; then
	scan_dir "$path";
    else
	relocate_file "$@";
    fi

    usleep 500000 # 500ms
}

main "$1"
