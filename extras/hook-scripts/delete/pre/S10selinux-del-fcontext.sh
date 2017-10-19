#!/bin/bash
#
# Install to hooks/<HOOKS_VER>/delete/pre
#
# Delete the file context associated with the brick path on volume deletion. The
# associated file context was added during volume creation.
#
# We do not explicitly relabel the brick, as this could be time consuming and
# unnecessary.
#
###

PROGNAME="Sselinux"
OPTSPEC="volname:"
VOL=

function parse_args () {
        ARGS=$(getopt -o '' -l $OPTSPEC -n $PROGNAME -- "$@")
        eval set -- "$ARGS"

        while true; do
        case $1 in
        --volname)
         shift
         VOL=$1
         ;;
        *)
         shift
         break
         ;;
        esac
        shift
        done
}

function delete_brick_fcontext()
{
        volname=$1

        # grab the path for each local brick
        brickdirs=$(grep '^path=' /var/lib/glusterd/vols/${volname}/bricks/* | cut -d= -f 2)

        for b in $brickdirs
        do
                # remove the file context associated with the brick path
                semanage fcontext --delete $b\(/.*\)?
        done
}

SELINUX_STATE=$(which getenforce && getenforce)
[ "${SELINUX_STATE}" = 'Disabled' ] && exit 0

parse_args "$@"
[ -z "$VOL" ] && exit 1

delete_brick_fcontext $VOL

# failure to delete the fcontext is not fatal
exit 0
