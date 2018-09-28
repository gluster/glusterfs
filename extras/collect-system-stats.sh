#!/bin/bash
################################################################################
# Usage: collect-system-stats.sh <delay-in-seconds>
# This script starts sar/top/iostat/vmstat processes which collect system stats
# with the interval <delay-in-seconds> given as argument to the script. When
# the script is stopped either by entering any input or Ctrl+C the list of
# files where output is captured will be printed on the screen which can be
# observed to find any problems/bottlenecks.
###############################################################################

function stop_processes {
        echo "Stopping the monitoring processes"
        echo "sar pid:$sar_pid", "top pid: $top_pid", "iostat pid: $iostat_pid", "vmstat pid: $vmstat_pid"
        kill "$sar_pid" "$top_pid" "$iostat_pid" "$vmstat_pid"
        echo "Files created: ${timestamp}-network.out, ${timestamp}-top.out, ${timestamp}-iostat.out, ${timestamp}-vmstat.out"
}

function check_dependent_commands_exist()
{
        declare -a arr=("sar" "top" "iostat" "vmstat")
        for i in "${arr[@]}"
        do
                if ! command -v "$i" > /dev/null 2>&1
                then
                        echo "ERROR: '$i' command is not found"
                        exit 1
                fi
        done

}

case "$1" in
    ''|*[!0-9]*) echo "Usage: $0 <delay-between-successive-metrics-collection-in-seconds>"; exit 1 ;;
    *) interval="$1" ;;
esac

timestamp=$(date +"%s")

check_dependent_commands_exist
sar -n DEV "$interval" > "${timestamp}"-network.out &
sar_pid="$!"
top -bHd "$interval" > "${timestamp}"-top.out &
top_pid="$!"
iostat -Ntkdx "$interval" > "${timestamp}"-iostat.out &
iostat_pid="$!"
vmstat -t "$interval" > "${timestamp}"-vmstat.out &
vmstat_pid="$!"
echo "Started sar, vmstat, iostat, top for collecting stats"


trap stop_processes EXIT
read -r -p "Press anything and ENTER to exit";
