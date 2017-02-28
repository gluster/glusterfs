#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup

TEST glusterd

TEST $CLI volume create ${V0} ${H0}:${B0}/brick0
EXPECT 'Created' volinfo_field ${V0} 'Status'

TEST $CLI volume start ${V0}
EXPECT 'Started' volinfo_field ${V0} 'Status'

TEST build_tester $(dirname ${0})/gfapi-load-volfile.c -lgfapi

sed -e "s,@@HOSTNAME@@,${H0},g" -e "s,@@BRICKPATH@@,${B0}/brick0,g" \
            $(dirname ${0})/protocol-client.vol.in \
            > $(dirname ${0})/protocol-client.vol

TEST ./$(dirname ${0})/gfapi-load-volfile \
             $(dirname ${0})/protocol-client.vol

cleanup_tester $(dirname ${0})/gfapi-load-volfile
cleanup_tester $(dirname ${0})/protocol-client.vol

cleanup
