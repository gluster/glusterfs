#!/bin/bash

USAGE="This script provides a utility to control CPU utilization for any
gluster daemon.In this, we use cgroup framework to configure CPU quota
for a process(like selfheal daemon). Before running this script, make
sure that daemon is running.Every time daemon restarts, it is required
to rerun this command to set CPU quota on new daemon process id.
User can enter any value between 10 to 100 for CPU quota.
Recommended value of quota period is 25. 25 means, kernel will allocate
25 ms period to this group of tasks in every 100 ms period. This 25ms
could be considered as the maximum percentage of CPU quota daemon can take.
This value will be reflected on CPU usage of "top" command.If provided pid
is the only process and no other process is in competition to get CPU, more
 than 25% could be allocated to daemon to speed up the process."

if [  $# -ge 1 ]; then
  case $1 in
    -h|--help) echo " " "$USAGE" | sed -r -e 's/^[ ]+//g'
               exit 0;
               ;;
  *) echo "Please Provide correct input for script."
     echo "For help correct options are -h or --help."
     exit 1;
               ;;
  esac
fi

DIR_EXIST=0
LOC="/sys/fs/cgroup/cpu,cpuacct/system.slice/glusterd.service"
echo "Enter gluster daemon pid for which you want to control CPU."
read daemon_pid

if expr ${daemon_pid} + 0 > /dev/null 2>&1 ;then
  CHECK_PID=$(pgrep -f gluster | grep ${daemon_pid})
  if [ -z "${CHECK_PID}" ]; then
    echo "No daemon is running or pid ${daemon_pid} does not match."
    echo "with running gluster processes."
    exit 1
  fi
else
  echo "Entered daemon_pid is not numeric so Rerun the script."
  exit 1
fi


if [ -f ${LOC}/tasks ];then
  CHECK_CGROUP=$(grep ${daemon_pid} ${LOC}/tasks)
  if [ ${CHECK_CGROUP} ]; then
    echo "pid ${daemon_pid} is attached with glusterd.service cgroup."
  fi
fi

cgroup_name=cgroup_gluster_${daemon_pid}
if [ -f ${LOC}/${cgroup_name}/tasks ]; then
  CHECK_CGROUP=$(grep ${daemon_pid} ${LOC}/${cgroup_name}/tasks)
  if [ ${CHECK_CGROUP} ]; then
    val=`cat ${LOC}/${cgroup_name}/cpu.cfs_quota_us`
    qval=$((val / 1000))
    echo "pid ${daemon_pid} is already attached ${cgroup_name} with quota value ${qval}."
    echo "Press n if you don't want to reassign ${daemon_pid} with new quota value."
    DIR_EXIST=1
  else
    echo "pid ${daemon_pid} is not attached with ${cgroup_name}."
  fi
fi

read -p "If you want to continue the script to attach ${daemon_pid} with new ${cgroup_name} cgroup Press (y/n)?" choice
case "$choice" in
  y|Y ) echo "yes";;
  n|N ) echo "no";exit;;
  * ) echo "invalid";exit;;
esac

systemctl set-property glusterd.service CPUShares=1024

if [ ${DIR_EXIST} -eq 0 ];then
  echo "Creating child cgroup directory '${cgroup_name} cgroup' for glusterd.service."
  mkdir -p ${LOC}/${cgroup_name}
  if [ ! -f ${LOC}/${cgroup_name}/tasks ];then
    echo "Not able to create ${cgroup_name} directory so exit."
    exit 1
  fi
fi

echo "Enter quota value in range [10,100]:  "

read quota_value
if expr ${quota_value} + 0 > /dev/null 2>&1 ;then
  if [ ${quota_value} -lt 10 ] || [ ${quota_value} -gt 100 ]; then
    echo "Entered quota value is not correct,it should be in the range ."
    echo "10-100. Ideal value is 25."
    echo "Rerun the sript with correct value."
    exit 1
  else
    echo "Entered quota value is $quota_value"
  fi
else
  echo "Entered quota value is not numeric so Rerun the script."
  exit 1
fi

quota_value=$((quota_value * 1000))
echo "Setting $quota_value to cpu.cfs_quota_us for gluster_cgroup."
echo ${quota_value} > ${LOC}/${cgroup_name}/cpu.cfs_quota_us

if ps -T -p ${daemon_pid} | grep gluster > /dev/null; then
  for thid in `ps -T -p ${daemon_pid} | grep gluster | awk -F " " '{print $2}'`;
    do
      echo ${thid} > ${LOC}/${cgroup_name}/tasks ;
    done
  if cat /proc/${daemon_pid}/cgroup | grep -w ${cgroup_name} > /dev/null; then
    echo "Tasks are attached successfully specific to ${daemon_pid} to ${cgroup_name}."
  else
    echo "Tasks are not attached successfully."
  fi
fi
