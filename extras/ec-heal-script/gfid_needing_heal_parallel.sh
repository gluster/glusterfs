#!/bin/bash
#  Copyright (c) 2019-2020 Red Hat, Inc. <http://www.redhat.com>
#  This file is part of GlusterFS.
#
#  This file is licensed to you under your choice of the GNU Lesser
#  General Public License, version 3 or any later version (LGPLv3 or
#  later), or the GNU General Public License, version 2 (GPLv2), in all
#  cases as published by the Free Software Foundation.

# This script provides a list of all the files which can be healed or not healed.
# It also generates two files, potential_heal and can_not_heal, which contains the information
# of all theose files. These files could be used by correct_pending_heals.sh to correct
# the fragmnets so that files could be healed by shd.

CAN_NOT_HEAL="can_not_heal"
CAN_HEAL="potential_heal"
LINE_SEP="==================================================="
LOG_DIR="/var/log/glusterfs"
LOG_FILE="$LOG_DIR/ec-heal-script.log"

function heal_log()
{
    echo "$1" >> "$LOG_FILE"
}

function _init ()
{
    if [ $# -ne 1 ]; then
    echo "usage: $0 <gluster volume name>";
    echo "This script provides a list of all the files which can be healed or not healed.
It also generates two files, potential_heal and can_not_heal, which contains the information
of all theose files. These files could be used by correct_pending_heals.sh to correct
the fragmnets so that files could be healed by shd."
    exit 2;
    fi

    volume=$1;
}

function get_pending_entries ()
{
    local volume_name=$1

    gluster volume heal "$volume_name" info | grep -v ":/" | grep -v "Number of entries" | grep -v "Status:" | sort -u | sed '/^$/d'
}

function get_entry_path_on_brick()
{
    local path="$1"
    local gfid_string=""
    if [[ "${path:0:1}" == "/" ]];
    then
        echo "$path"
    else
        gfid_string="$(echo "$path" | cut -f2 -d':' | cut -f1 -d '>')"
        echo "/.glusterfs/${gfid_string:0:2}/${gfid_string:2:2}/$gfid_string"
    fi
}

function run_command_on_server()
{
	local subvolume="$1"
	local host="$2"
	local cmd="$3"
	local output
        output=$(ssh -n "${host}" "${cmd}")
	if [ -n "$output" ]
	then
            echo "$subvolume:$output"
	fi
}

function get_entry_path_all_bricks ()
{
    local entry="$1"
    local bricks="$2"
    local cmd=""
    for brick in $bricks
    do
	    echo "${brick}#$(get_entry_path_on_brick "$entry")"
    done | tr '\n' ','
}

function get_stat_for_entry_from_all_bricks ()
{
    local entry="$1"
    local bricks="$2"
    local subvolume=0
    local host=""
    local bpath=""
    local cmd=""

    for brick in $bricks
    do
        if [[ "$((subvolume % 6))" == "0" ]]
        then
            subvolume=$((subvolume+1))
        fi
        host=$(echo "$brick" | cut -f1 -d':')
        bpath=$(echo "$brick" | cut -f2 -d':')

	cmd="stat --format=%F:%B:%s $bpath$(get_entry_path_on_brick "$entry") 2>/dev/null"
    run_command_on_server "$subvolume" "${host}" "${cmd}" &
    done | sort | uniq -c | sort -rnk1
}

function get_bricks_from_volume()
{
    local v=$1
    gluster volume info "$v" | grep -E "^Brick[0-9][0-9]*:" | cut -f2- -d':'
}

function print_entry_gfid()
{
    local host="$1"
    local dirpath="$2"
    local entry="$3"
    local gfid
    gfid="$(ssh -n "${host}" "getfattr -d -m. -e hex $dirpath/$entry 2>/dev/null | grep trusted.gfid=|cut -f2 -d'='")"
    echo "$entry" - "$gfid"
}

function print_brick_directory_info()
{
    local h="$1"
    local dirpath="$2"
    while read -r e
    do
        print_entry_gfid "${h}" "${dirpath}" "${e}"
    done < <(ssh -n "${h}" "ls $dirpath 2>/dev/null")
}

function print_directory_info()
{
    local entry="$1"
    local bricks="$2"
    local h
    local b
    local gfid
    for brick in $bricks;
    do
        h="$(echo "$brick" | cut -f1 -d':')"
        b="$(echo "$brick" | cut -f2 -d':')"
        dirpath="$b$(get_entry_path_on_brick "$entry")"
	print_brick_directory_info "${h}" "${dirpath}" &
    done | sort | uniq -c
}

function print_entries_needing_heal()
{
    local quorum=0
    local entry="$1"
    local bricks="$2"
    while read -r line
    do
        quorum=$(echo "$line" | awk '{print $1}')
        if [[ "$quorum" -lt 4 ]]
        then
            echo "$line - Not in Quorum"
        else
            echo "$line - In Quorum"
        fi
    done < <(print_directory_info "$entry" "$bricks")
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

function main ()
{
    local bricks=""
    local quorum=0
    local stat_info=""
    local file_type=""
    local gfid_actual_paths=""
    local bpath=""
    local file_paths=""
    local good=0
    local bad=0
    bricks=$(get_bricks_from_volume "$volume")
    rm -f "$CAN_HEAL"
    rm -f "$CAN_NOT_HEAL"
    mkdir "$LOG_DIR" -p

    heal_log "Started $(basename "$BASH_SOURCE") on $(date) "
    while read -r heal_entry
    do
        heal_log "------------------------------------------------------------------"
        heal_log "$heal_entry"

        gfid_actual_paths=""
        file_paths="$(get_entry_path_all_bricks "$heal_entry" "$bricks")"
        stat_info="$(get_stat_for_entry_from_all_bricks "$heal_entry" "$bricks")"
        heal_log "$stat_info"

        quorum=$(echo "$stat_info" | head -1 | awk '{print $1}')
        good_stat=$(echo "$stat_info" | head -1 | awk '{print $3}')
        file_type="$(echo "$stat_info" | head -1 | cut -f2 -d':')"
        if [[ "$file_type" == "directory" ]]
        then
            print_entries_needing_heal "$heal_entry" "$bricks"
        else
            if [[ "$quorum" -ge 4 ]]
            then
                good=$((good + 1))
                heal_log "Verdict: Healable"

                echo "${good_stat}|$file_paths" >> "$CAN_HEAL"
            else
                bad=$((bad + 1))
                heal_log "Verdict: Not Healable"
                for bpath in ${file_paths//,/ }
                do
                    if [[ -z "$gfid_actual_paths" ]]
                    then
                        gfid_actual_paths=$(find_file_paths "$bpath")
                    else
                        break
                    fi
                done
                log_can_not_heal "$gfid_actual_paths" "${file_paths}"
            fi
        fi
    done < <(get_pending_entries "$volume")
    heal_log "========================================="
    heal_log "Total number of  potential heal : ${good}"
    heal_log "Total number of can not heal    : ${bad}"
    heal_log "========================================="
}

_init "$@" && main "$@"
