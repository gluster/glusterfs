#!/bin/bash
#
# Install to hooks/<HOOKS_VER>/create/post
#
# Add an SELinux file context for each brick using the glusterd_brick_t type.
# This ensures that the brick is relabeled correctly on an SELinux restart or
# restore. Subsequently, run a restore on the brick path to set the selinux
# labels.
#
###

PROGNAME="Sselinux"
OPTSPEC="volname:"
VOL=

parse_args () {
  ARGS=$(getopt -o '' -l ${OPTSPEC} -n ${PROGNAME} -- "$@")
  eval set -- "${ARGS}"

  while true; do
    case ${1} in
      --volname)
        shift
        VOL=${1}
      ;;
      *)
        shift
        break
      ;;
    esac
    shift
  done
}

set_brick_labels()
{
  volname=${1}

  # grab the path for each local brick
  brickpath="/var/lib/glusterd/vols/${volname}/bricks/*"
  brickdirs=$(grep '^path=' "${brickpath}" | cut -d= -f 2 | sort -u)

  for b in ${brickdirs}; do
    # Add a file context for each brick path and associate with the
    # glusterd_brick_t SELinux type.
    pattern="${b}\(/.*\)?"
    semanage fcontext --add -t glusterd_brick_t -r s0 "${pattern}"

    # Set the labels on the new brick path.
    restorecon -R "${b}"
  done
}

SELINUX_STATE=$(which getenforce && getenforce)
[ "${SELINUX_STATE}" = 'Disabled' ] && exit 0

parse_args "$@"
[ -z "${VOL}" ] && exit 1

set_brick_labels "${VOL}"

exit 0
