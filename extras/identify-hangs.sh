#!/bin/bash
function get_statedump_fnames_without_timestamps
{
    ls | grep -E "[.]dump[.][0-9][0-9]*" | cut -f1-3 -d'.' | sort -u
}

function get_non_uniq_fields
{
    local statedump_fname_prefix=$1
    print_stack_lkowner_unique_in_one_line "$statedump_fname_prefix" | sort | uniq -c | grep -vE "^\s*1 " | awk '{$1="repeats="$1; print $0}'
}

function print_stack_lkowner_unique_in_one_line
{
    local statedump_fname_prefix=$1
    sed -e '/./{H;$!d;}' -e 'x;/unique=/!d;/stack=/!d;/lk-owner=/!d;/pid=/!d;' "${statedump_fname_prefix}"* | grep -E "(stack|lk-owner|unique|pid)=" | paste -d " " - - - -
}

function get_stacks_that_appear_in_multiple_statedumps
{
    #If a stack with same 'unique/lk-owner/stack' appears in multiple statedumps
    #print the stack
    local statedump_fname_prefix=$1
    while read -r non_uniq_stack;
    do
        if [ -z "$printed" ];
        then
            printed="1"
        fi
        echo "$statedump_fname_prefix" "$non_uniq_stack"
    done < <(get_non_uniq_fields "$statedump_fname_prefix")
}

statedumpdir=${1}
if [ -z "$statedumpdir" ];
then
    echo "Usage: $0 <statedump-dir>"
    exit 1
fi

if [ ! -d "$statedumpdir" ];
then
    echo "$statedumpdir: Is not a directory"
    echo "Usage: $0 <statedump-dir>"
    exit 1
fi

cd "$statedumpdir" || exit 1
for statedump_fname_prefix in $(get_statedump_fnames_without_timestamps);
do
    get_stacks_that_appear_in_multiple_statedumps "$statedump_fname_prefix"
done | column -t
echo "NOTE: stacks with lk-owner=\"\"/lk-owner=0000000000000000/unique=0 may not be hung frames and need further inspection" >&2
