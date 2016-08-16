#!/usr/bin/env bash
# Description:  GlusterFS volumes are mounted in the background
#               when interfaces are brought up; this script waits
#               for them to be mounted before carrying on.
#
# Author: Sebastian Kraetzig <info@ts3-tools.info>
# Version: 2016-08-15
#

BOOLEAN=true;

while ${BOOLEAN}; do
        VOLUME_STATUS=$(gluster volume info | grep -i status | head -1 | cut -d ':' -f 2 | tr -d '[:space:]')

        if [[ ${VOLUME_STATUS} == "Started" ]]; then
                if [[ $(mount -a -t glusterfs,fuse.glusterfs) ]]; then
                        BOOLEAN=false;
                fi
        fi
done

exit 0;
