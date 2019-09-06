#!/bin/bash
#  Copyright (c) 2019-2020 Red Hat, Inc. <http://www.redhat.com>
#  This file is part of GlusterFS.
#
#  This file is licensed to you under your choice of the GNU Lesser
#  General Public License, version 3 or any later version (LGPLv3 or
#  later), or the GNU General Public License, version 2 (GPLv2), in all
#  cases as published by the Free Software Foundation.

# This script finally resets the xattrs of all the fragments of a file
# which can be healed as per gfid_needing_heal_parallel.sh.
# gfid_needing_heal_parallel.sh will produce two files, potential_heal and can_not_heal.
# This script takes potential_heal as input and resets xattrs of all the fragments
# of those files present in this file and which could be healed as per
# trusted.ec.size xattar of the file else it will place the entry in can_not_heal
# file.  Those entries which must be healed will be place in must_heal file
# after setting xattrs so that user can track those files.


MOD_BACKUP_FILES="modified_and_backedup_files"
CAN_NOT_HEAL="can_not_heal"
LOG_DIR="/var/log/glusterfs"
LOG_FILE="$LOG_DIR/ec-heal-script.log"
LINE_SEP="==================================================="

function heal_log()
{
    echo "$1" >> "$LOG_FILE"
}

function desc ()
{
    echo ""
    echo "This script finally resets the xattrs of all the fragments of a file
which can be healed as per gfid_needing_heal_parallel.sh.
gfid_needing_heal_parallel.sh will produce two files, potential_heal and can_not_heal.
This script takes potential_heal as input and resets xattrs of all the fragments
of those files present in this file and which could be healed as per
trusted.ec.size xattar of the file else it will place the entry in can_not_heal
file.  Those entries which must be healed will be place in must_heal file
after setting xattrs so that user can track those files."
}

function _init ()
{
    if [ $# -ne 0 ]
    then
        echo "usage: $0"
        desc
        exit 2
    fi

    if [ ! -f "potential_heal" ]
    then
        echo "Nothing to correct. File "potential_heal" does not exist"
        echo ""
        desc
        exit 2
    fi
}

function total_file_size_in_hex()
{
    local frag_size=$1
    local size=0
    local hex_size=""

    size=$((frag_size * 4))
    hex_size=$(printf '0x%016x' $size)
    echo "$hex_size"
}

function backup_file_fragment()
{
    local file_host=$1
    local file_entry=$2
    local gfid_actual_paths=$3
    local brick_root=""
    local temp=""
    local backup_dir=""
    local cmd=""
    local gfid=""

    brick_root=$(echo "$file_entry" | cut -d "#" -f 1)
    temp=$(echo "$(basename "$BASH_SOURCE")" | cut -d '.' -f 1)
    backup_dir=$(echo "${brick_root}/.glusterfs/${temp}")
    file_entry=${file_entry//#}

    gfid=$(echo "${gfid_actual_paths}" | cut -d '|' -f 1 | cut -d '/' -f 5)
    echo "${file_host}:${backup_dir}/${gfid}" >> "$MOD_BACKUP_FILES"

    cmd="mkdir -p ${backup_dir} && yes | cp -af ${file_entry} ${backup_dir}/${gfid} 2>/dev/null"
    ssh -n "${file_host}" "${cmd}"
}

function set_frag_xattr ()
{
    local file_host=$1
    local file_entry=$2
    local good=$3
    local cmd1=""
    local cmd2=""
    local cmd=""
    local version="0x00000000000000010000000000000001"
    local dirty="0x00000000000000010000000000000001"

    if [[ $good -eq 0 ]]
    then
            version="0x00000000000000000000000000000000"
    fi

    cmd1=" setfattr -n trusted.ec.version -v ${version} ${file_entry} &&"
    cmd2=" setfattr -n trusted.ec.dirty -v ${dirty} ${file_entry}"
    cmd=${cmd1}${cmd2}
    ssh -n "${file_host}" "${cmd}"
}

function set_version_dirty_xattr ()
{
    local file_paths=$1
    local good=$2
    local gfid_actual_paths=$3
    local file_entry=""
    local file_host=""
    local bpath=""

    for bpath in ${file_paths//,/ }
    do
        file_host=$(echo "$bpath" | cut -d ":" -f 1)
        file_entry=$(echo "$bpath" | cut -d ":" -f 2)
        backup_file_fragment "$file_host" "$file_entry" "$gfid_actual_paths"
        file_entry=${file_entry//#}
        set_frag_xattr "$file_host" "$file_entry" "$good"
    done
}

function match_size_xattr_quorum ()
{
    local file_paths=$1
    local file_entry=""
    local file_host=""
    local cmd=""
    local size_xattr=""
    local bpath=""
    declare -A xattr_count

    for bpath in ${file_paths//,/ }
    do
        size_xattr=""
        file_host=$(echo "$bpath" | cut -d ":" -f 1)
        file_entry=$(echo "$bpath" | cut -d ":" -f 2)
        file_entry=${file_entry//#}

        cmd="getfattr -n trusted.ec.size -d -e hex ${file_entry} 2>/dev/null | grep -w "trusted.ec.size" | cut -d '=' -f 2"
        size_xattr=$(ssh -n "${file_host}" "${cmd}")
        if [[ -n $size_xattr ]]
        then
            count=$((xattr_count["$size_xattr"] + 1))
            xattr_count["$size_xattr"]=${count}
            if [[ $count -ge 4 ]]
            then
                echo "${size_xattr}"
                return
            fi
        fi
    done
    echo "False"
}

function match_version_xattr ()
{
    local file_paths=$1
    local file_entry=""
    local file_host=""
    local cmd=""
    local version=""
    local bpath=""
    declare -A ver_count

    for bpath in ${file_paths//,/ }
    do
        version=""
        file_host=$(echo "$bpath" | cut -d ":" -f 1)
        file_entry=$(echo "$bpath" | cut -d ":" -f 2)
        file_entry=${file_entry//#}

        cmd="getfattr -n trusted.ec.version -d -e hex ${file_entry} 2>/dev/null | grep -w "trusted.ec.version" | cut -d '=' -f 2"
        version=$(ssh -n "${file_host}" "${cmd}")
        ver_count["$version"]=$((ver_count["$version"] + 1))
    done
    for key in "${ver_count[@]}"
    do
        if [[ $key -ge 4 ]]
        then
            echo "True"
            return
        else
            echo "False"
            return
        fi
    done
}

function match_stat_size_with_xattr ()
{
    local bpath=$1
    local size=$2
    local file_stat=$3
    local xattr=$4
    local file_entry=""
    local file_host=""
    local cmd=""
    local stat_output=""
    local hex_size=""

    file_host=$(echo "$bpath" | cut -d ":" -f 1)
    file_entry=$(echo "$bpath" | cut -d ":" -f 2)

    file_entry=${file_entry//#}
    cmd="stat --format=%F:%B:%s $file_entry 2>/dev/null"
    stat_output=$(ssh -n "${file_host}" "${cmd}")
    echo "$stat_output" | grep -w "${file_stat}" > /dev/null

    if [[ $? -eq 0 ]]
    then
        cmd="getfattr -n trusted.ec.size -d -e hex ${file_entry} 2>/dev/null | grep -w "trusted.ec.size" | cut -d '=' -f 2"
        hex_size=$(ssh -n "${file_host}" "${cmd}")

        if [[ -z $hex_size || "$hex_size" != "$xattr" ]]
        then
            echo "False"
            return
        fi
        size_diff=$(printf '%d' $(( size - hex_size )))
        if [[ $size_diff -gt 2047 ]]
        then
            echo "False"
            return
        else
            echo "True"
            return
        fi
    else
        echo "False"
        return
    fi
}

function find_file_paths ()
{
    local bpath=$1
    local file_entry=""
    local file_host=""
    local cmd=""
    local brick_root=""
    local gfid=""
    local actual_path=""
    local gfid_path=""

    file_host=$(echo "$bpath" | cut -d ":" -f 1)
    file_entry=$(echo "$bpath" | cut -d ":" -f 2)
    brick_root=$(echo "$file_entry" | cut -d "#" -f 1)

    gfid=$(echo "${file_entry}" | grep ".glusterfs")
    if [[ -n "$gfid" ]]
    then
        gfid_path=$(echo "$file_entry" | cut -d "#" -f 2)
        file_entry=${file_entry//#}
        cmd="find -L '$brick_root' -samefile '$file_entry' 2>/dev/null | grep -v '.glusterfs' "
        actual_path=$(ssh -n "${file_host}" "${cmd}")
        #removing absolute path so that user can refer this from mount point
        actual_path=${actual_path#"$brick_root"}
    else
        actual_path=$(echo "$file_entry" | cut -d "#" -f 2)
        file_entry=${file_entry//#}
        cmd="find -L '$brick_root' -samefile '$file_entry' 2>/dev/null | grep '.glusterfs' "
        gfid_path=$(ssh -n "${file_host}" "${cmd}")
        gfid_path=${gfid_path#"$brick_root"}
    fi

    echo "${gfid_path}|${actual_path}"
}

function log_can_not_heal ()
{
    local gfid_actual_paths=$1
    local file_paths=$2
    file_paths=${file_paths//#}

    echo "${LINE_SEP}" >> "$CAN_NOT_HEAL"
    echo "Can Not Heal : $(echo "$gfid_actual_paths" | cut -d '|' -f 2)" >> "$CAN_NOT_HEAL"
    for bpath in ${file_paths//,/ }
    do
        echo "${bpath}" >> "$CAN_NOT_HEAL"
    done
}

function check_all_frag_and_set_xattr ()
{
    local file_paths=$1
    local total_size=$2
    local file_stat=$3
    local bpath=""
    local healthy_count=0
    local match="False"
    local matching_bricks=""
    local bad_bricks=""
    local gfid_actual_paths=""

    for bpath in ${file_paths//,/ }
    do
        if [[ -n "$gfid_actual_paths" ]]
        then
            break
        fi
        gfid_actual_paths=$(find_file_paths "$bpath")
    done

   match=$(match_size_xattr_quorum "$file_paths")

#   echo "${match} : $bpath" >> "$MOD_BACKUP_FILES"

    if [[ "$match" != "False" ]]
    then
        xattr="$match"
        for bpath in ${file_paths//,/ }
        do
            match="False"
            match=$(match_stat_size_with_xattr "$bpath" "$total_size" "$file_stat" "$xattr")
            if [[ "$match" == "True" ]]
            then
                matching_bricks="${bpath},${matching_bricks}"
                healthy_count=$((healthy_count + 1))
            else
                bad_bricks="${bpath},${bad_bricks}"
            fi
        done
    fi

    if [[ $healthy_count -ge 4 ]]
    then
        match="True"
        echo "${LINE_SEP}" >> "$MOD_BACKUP_FILES"
        echo "Modified : $(echo "$gfid_actual_paths" | cut -d '|' -f 2)" >> "$MOD_BACKUP_FILES"
        set_version_dirty_xattr  "$matching_bricks" 1 "$gfid_actual_paths"
        set_version_dirty_xattr  "$bad_bricks" 0 "$gfid_actual_paths"
    else
        log_can_not_heal "$gfid_actual_paths" "${file_paths}"
    fi

    echo "$match"
}
function set_xattr()
{
    local count=$1
    local heal_entry=""
    local file_stat=""
    local frag_size=""
    local total_size=""
    local file_paths=""
    local num=""
    local can_heal_count=0

    heal_log "Started $(basename $BASH_SOURCE) on $(date) "

    while read -r heal_entry
    do
        heal_log "$LINE_SEP"
        heal_log "${heal_entry}"

        file_stat=$(echo "$heal_entry" | cut -d "|" -f 1)
        frag_size=$(echo "$file_stat" | rev | cut -d ":" -f 1 | rev)
        total_size="$(total_file_size_in_hex "$frag_size")"
        file_paths=$(echo "$heal_entry" | cut -d "|" -f 2)
        match=$(check_all_frag_and_set_xattr "$file_paths" "$total_size" "$file_stat")
        if [[ "$match" == "True" ]]
        then
            can_heal_count=$((can_heal_count + 1))
        fi

        sed -i '1d' potential_heal
        count=$((count - 1))
        if [ $count == 0 ]
        then
            num=$(cat potential_heal | wc -l)
            heal_log "$LINE_SEP"
            heal_log "${1} : Processed"
            heal_log "${can_heal_count} : Modified to Heal"
            heal_log "$((${1} - can_heal_count)) : Moved to can_not_heal."
            heal_log "${num} : Pending as Potential Heal"
            exit 0
        fi

    done < potential_heal
}

function main ()
{
    local count=0

    read -p "Number of files to correct: [choose between 1-1000] (0 for All):" count
    if [[ $count -lt 0 || $count -gt 1000 ]]
    then
        echo "Provide correct value:"
        exit 2
    fi

    if [[ $count -eq 0 ]]
    then
        count=$(cat potential_heal | wc -l)
    fi
    set_xattr "$count"
}

_init "$@" && main "$@"
