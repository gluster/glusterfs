#!/bin/bash

. $(dirname $0)/../../include.rc

cleanup;

TEST glusterd
TEST pidof glusterd
TEST $CLI volume info;
# Distributed volume with a single brick was chosen solely for the ease of
#implementing the test case (to be precise, for the ease of extracting the port number).
TEST $CLI volume create $V0 $H0:$B0/brick0;

TEST $CLI volume start $V0;

function port_field()
{
    local vol=$1;
    local opt=$2;
    if [ $opt -eq '0' ]; then
        $CLI volume status $vol | grep "brick0" | awk '{print $3}';
    else
        $CLI volume status $vol detail | grep "^TCP Port " | awk '{print $4}';
    fi
}

function xml_port_field()
{
    local vol=$1;
    local opt=$2;
        $CLI --xml volume status $vol $opt | tr -d '\n' |\
#Find the first occurrence of the string between <port> and </port>
        sed -rn 's/<port>/&###/;s/<\/port>/###&/;s/^.*###(.*)###.*$/\1/p'
}

TEST $CLI volume status $V0;
TEST $CLI volume status $V0 detail;
TEST $CLI --xml volume status $V0;
TEST $CLI --xml volume status $V0 detail;

# Kill the brick process. After this, port number for the killed (in this case brick) process must be "N/A".
kill `cat $GLUSTERD_WORKDIR/vols/$V0/run/$H0-d-backends-brick0.pid`

EXPECT "N/A" port_field $V0 '0'; # volume status
EXPECT "N/A" port_field $V0 '1'; # volume status detail

EXPECT "N/A" xml_port_field $V0 '';
EXPECT "N/A" xml_port_field $V0 'detail';

cleanup;
