#!/bin/bash
# Usage:
# nice -n -19 strace-brick.sh glusterfsd 50

brick_process_name=$1
min_watch_cpu=$2
if [ ! $brick_process_name ]; then
        brick_process_name=glusterfsd
fi

if [ ! $min_watch_cpu ]; then
        min_watch_cpu=50
fi

echo "min_watch_cpu: $min_watch_cpu"

break=false

while ! $break;
do
        mypids=( $(pgrep $brick_process_name) )
        echo "mypids: ${mypids[*]}"

        pid_args=$(echo ${mypids[*]} | sed -e 's/ / -p /g;s/^/-p /')
        echo "pid_args: $pid_args"

        pcpu=( $(ps $pid_args -o pcpu -h ) )
        echo "pcpu: ${pcpu[*]}"

        wait_longer=false

        for i in $( seq 0 $((${#pcpu[*]} - 1)) )
        do
                echo "i: $i"
                echo "mypids[$i]: ${mypids[$i]}"

                int_pcpu=$(echo ${pcpu[$i]} | cut -f 1 -d '.')
                echo "int_pcpu: $int_pcpu"
                if [ ! $int_pcpu ] || [ ! $min_watch_cpu ]; then
                        break=true
                        echo "breaking"
                fi
                if [ $int_pcpu -ge $min_watch_cpu ]; then
                        wait_longer=true
                        mydirname="${brick_process_name}-${mypids[$i]}-$(date --utc +'%Y%m%d-%H%M%S.%N')"
                        $(mkdir $mydirname && cd $mydirname && timeout --kill-after=5 --signal=KILL 60 nice -n -19 strace -p ${mypids[$i]} -ff -tt -T -o $brick_process_name) &
                fi
        done

        if $wait_longer; then
                sleep 90
        else
                sleep 15
        fi
done
