#!/bin/bash

. $(dirname $0)/../include.rc

SCRIPT_TIMEOUT=500

###############################################################################################
#Before upgrade
###############################################################################################
brick=/bricks/brick1
epoch1=$(date '+%s')
sleep 1
epoch2=$(date '+%s')
mkdir -p /bricks/brick1/.glusterfs/changelogs/htime
mkdir -p /bricks/brick1/.glusterfs/changelogs

#multiple htime files(changelog enable/disable scenario)
TEST touch /bricks/brick1/.glusterfs/changelogs/htime/HTIME.$epoch1
TEST touch /bricks/brick1/.glusterfs/changelogs/htime/HTIME.$epoch2

#changelog files
TEST touch /bricks/brick1/.glusterfs/changelogs/CHANGELOG.$epoch1
TEST touch /bricks/brick1/.glusterfs/changelogs/CHANGELOG.$epoch2

htime_file1=/bricks/brick1/.glusterfs/changelogs/htime/HTIME.$epoch1
htime_file2=/bricks/brick1/.glusterfs/changelogs/htime/HTIME.$epoch2

#data inside htime files before upgrade
data1=/bricks/brick1/.glusterfs/changelogs/CHANGELOG.$epoch1
data2=/bricks/brick1/.glusterfs/changelogs/CHANGELOG.$epoch2

#data inside htime files after upgrade
updated_data1=/bricks/brick1/.glusterfs/changelogs/`echo $(date '+%Y/%m/%d')`/CHANGELOG.$epoch1
updated_data2=/bricks/brick1/.glusterfs/changelogs/`echo $(date '+%Y/%m/%d')`/CHANGELOG.$epoch2

echo -n $data1>$htime_file1
echo -n $data2>$htime_file2

echo "Before upgrade:"
EXPECT '1' echo $(grep $data1 $htime_file1 | wc -l)
EXPECT '1' echo $(grep $data2 $htime_file2 | wc -l)

EXPECT '1' echo $(ls /bricks/brick1/.glusterfs/changelogs/CHANGELOG.$epoch1 | wc -l)
EXPECT '1' echo $(ls /bricks/brick1/.glusterfs/changelogs/htime/HTIME.$epoch1 | wc -l)
EXPECT '1' echo $(ls /bricks/brick1/.glusterfs/changelogs/CHANGELOG.$epoch2 | wc -l)
EXPECT '1' echo $(ls /bricks/brick1/.glusterfs/changelogs/htime/HTIME.$epoch2 | wc -l)
###############################################################################################
#Upgrade
###############################################################################################
### This needed to be fixed as this very vague finding a file with name in '/'
### multiple file with same name can exist
### for temp fix picking only 1st result
TEST upgrade_script=$(find / -type f -name glusterfs-georep-upgrade.py -print | head -n 1)
TEST python3 $upgrade_script $brick

###############################################################################################
#After upgrade
###############################################################################################
echo "After upgrade:"
EXPECT '1' echo $(grep $updated_data1 $htime_file1 | wc -l)
EXPECT '1' echo $(grep $updated_data2 $htime_file2 | wc -l)

#Check directory structure inside changelogs
TEST ! ls /bricks/brick1/.glusterfs/changelogs/CHANGELOG.$epoch1
EXPECT '1' echo $(ls /bricks/brick1/.glusterfs/changelogs/htime/HTIME.$epoch1 | wc -l)
EXPECT '1' echo $(ls /bricks/brick1/.glusterfs/changelogs/htime/HTIME.$epoch1.bak | wc -l)
EXPECT '1' echo $(ls /bricks/brick1/.glusterfs/changelogs/`echo $(date '+%Y')` | wc -l)
EXPECT '1' echo $(ls /bricks/brick1/.glusterfs/changelogs/`echo $(date '+%Y/%m')` | wc -l)
EXPECT '2' echo $(ls /bricks/brick1/.glusterfs/changelogs/`echo $(date '+%Y/%m/%d')` | wc -l)
EXPECT '1' echo $(ls /bricks/brick1/.glusterfs/changelogs/`echo $(date '+%Y/%m/%d')`/CHANGELOG.$epoch1 | wc -l)

TEST ! ls /bricks/brick1/.glusterfs/changelogs/CHANGELOG.$epoch2
EXPECT '1' echo $(ls /bricks/brick1/.glusterfs/changelogs/htime/HTIME.$epoch2 | wc -l)
EXPECT '1' echo $(ls /bricks/brick1/.glusterfs/changelogs/htime/HTIME.$epoch2.bak | wc -l)
EXPECT '1' echo $(ls /bricks/brick1/.glusterfs/changelogs/`echo $(date '+%Y')` | wc -l)
EXPECT '1' echo $(ls /bricks/brick1/.glusterfs/changelogs/`echo $(date '+%Y/%m')`| wc -l)
EXPECT '1' echo $(ls /bricks/brick1/.glusterfs/changelogs/`echo $(date '+%Y/%m/%d')`/CHANGELOG.$epoch2 | wc -l)

TEST rm -rf /bricks
