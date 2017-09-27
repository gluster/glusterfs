#!/bin/bash

USAGE="This commands provides a utility to control MEMORY utilization for any
gluster daemon.In this, we use cgroup framework to configure MEMORY limit for
a process. Before running this script, make sure that daemon is running.Every
time daemon restarts, it is required to rerun this command to set memory limit
(in bytes) on new daemon process id.User can enter any value between 100
(in Mega bytes) to 8000000000000 for Memory limit in Mega bytes.
Memory limit value is depends on how much maximum memory user wants to restrict
for specific daemon process.If a process will try to consume memore more than
configured value then cgroup will hang/sleep this task and to resume the task
rerun the script with new increase memory limit value ."

if [  $# -ge 1 ]; then
  case $1 in
    -h|--help) echo " " "$USAGE" | sed -r -e 's/^[ ]+//g'
               exit 0;
               ;;
    *) echo "Please Provide correct input for script."
       echo "For help correct options are -h of --help."
       exit 1;
               ;;
  esac
fi

DIR_EXIST=0
LOC="/sys/fs/cgroup/memory/system.slice/glusterd.service"
echo "Enter Any gluster daemon pid for that you want to control MEMORY."
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


if [ -f ${LOC}/tasks ]; then
  CHECK_CGROUP=$(grep ${daemon_pid} ${LOC}/tasks)
  if [ ${CHECK_CGROUP} ] ;then
    echo "pid ${daemon_pid} is attached with default glusterd.service cgroup."
  fi
fi

cgroup_name=cgroup_gluster_${daemon_pid}
if [ -f ${LOC}/${cgroup_name}/tasks ];then
  CHECK_CGROUP=$(grep ${daemon_pid} ${LOC}/${cgroup_name}/tasks)
  if [ ${CHECK_CGROUP} ]; then
    val=`cat ${LOC}/${cgroup_name}/memory.limit_in_bytes`
    mval=$((val / 1024 / 1024))
    echo "pid ${daemon_pid} is already attached ${cgroup_name} with mem value ${mval}."
    echo "Press n if you don't want to reassign ${daemon_pid} with new mem value."
    DIR_EXIST=1
  else
    echo "pid ${daemon_pid} is not attached with ${cgroup_name}."
  fi
fi

read -p "If you want to continue the script to attach daeomon with new cgroup. Press (y/n)?" choice
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
    echo "Not able to create ${LOC}/${cgroup_name} directory so exit."
    exit 1
  fi
fi

echo "Enter Memory value in Mega bytes [100,8000000000000]:  "

read mem_value
if expr ${mem_value} + 0 > /dev/null 2>&1 ;then
  if [ ${mem_value} -lt 100 ] || [ ${mem_value} -gt 8000000000000 ]; then
    echo "Entered memory value is not correct,it should be in the range ."
    echo "100-8000000000000, Rerun the script with correct value ."
    exit 1
  else
    echo "Entered memory limit value is ${mem_value}."
  fi
else
  echo "Entered memory value is not numeric so Rerun the script."
  exit 1
fi

mem_value=$(($mem_value * 1024 * 1024))
if [ ${DIR_EXIST} -eq 0 ];then
  echo "Setting ${mem_value} to memory.limit_in_bytes for ${LOC}/${cgroup_name}."
  echo ${mem_value} > ${LOC}/${cgroup_name}/memory.limit_in_bytes
  #Set memory value to memory.memsw.limit_in_bytes
  echo ${mem_value} > ${LOC}/${cgroup_name}/memory.memsw.limit_in_bytes
  # disable oom_control so that kernel will not send kill signal to the
  # task once limit has reached
  echo 1 > ${LOC}/${cgroup_name}/memory.oom_control
else
  #Increase mem_value to memory.memsw.limit_in_bytes
  echo ${mem_value} > ${LOC}/${cgroup_name}/memory.memsw.limit_in_bytes
  echo "Increase ${mem_value} to memory.limit_in_bytes for ${LOC}/${cgroup_name}."
  echo ${mem_value} > ${LOC}/${cgroup_name}/memory.limit_in_bytes
  # disable oom_control so that kernel will not send kill signal to the
  # task once limit has reached
  echo 1 > ${LOC}/${cgroup_name}/memory.oom_control
fi

if ps -T -p ${daemon_pid} | grep gluster > /dev/null; then
  for thid in `ps -T -p ${daemon_pid} | grep gluster | awk -F " " '{print $2}'`;
    do
      echo ${thid} > ${LOC}/${cgroup_name}/tasks ;
    done
  if cat /proc/${daemon_pid}/cgroup | grep -iw ${cgroup_name} > /dev/null; then
    echo "Tasks are attached successfully specific to ${daemon_pid} to ${cgroup_name}."
  else
    echo "Tasks are not attached successfully."
  fi
fi
