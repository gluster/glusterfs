#!/bin/bash
#
# Install to hooks/<HOOKS_VER>/add-brick/post
#
# Add an SELinux file context for each brick using the glusterd_brick_t type.
# This ensures that the brick is relabeled correctly on an SELinux restart or
# restore. Subsequently, run a restore on the brick path to set the selinux
# labels.
#
###

PROGNAME="Sselinux"
OPTSPEC="volname:,version:,gd-workdir:,volume-op:"
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
      --gd-workdir)
          shift
          GLUSTERD_WORKDIR=$1
          ;;
      --version)
          shift
          ;;
      --volume-op)
          shift
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
  local volname="${1}"
  local fctx
  local list=()

  fctx="$(semanage fcontext --list -C)"

  # wait for new brick path to be updated under
  # ${GLUSTERD_WORKDIR}/vols/${volname}/bricks/
  sleep 5

  # grab the path for each local brick
  brickpath="${GLUSTERD_WORKDIR}/vols/${volname}/bricks/"
  brickdirs=$(
    find "${brickpath}" -type f -exec grep '^path=' {} \; | \
    cut -d= -f 2 | \
    sort -u
  )

  # create a list of bricks for which custom SELinux
  # label doesn't exist
  for b in ${brickdirs}; do
    pattern="${b}(/.*)?"
    echo "${fctx}" | grep "^${pattern}\s" >/dev/null
    if [[ $? -ne 0 ]]; then
      list+=("${pattern}")
    fi
  done

  # Add a file context for each brick path in the list and associate with the
  # glusterd_brick_t SELinux type.
  for p in ${list[@]}
  do
    semanage fcontext --add -t glusterd_brick_t -r s0 "${p}"
  done

  # Set the labels for which SELinux label was added above
  for b in ${brickdirs}
  do
    echo "${list[@]}" | grep "${b}" >/dev/null
    if [[ $? -eq 0 ]]; then
      restorecon -R "${b}"
    fi
  done
}

SELINUX_STATE=$(which getenforce && getenforce)
[ "${SELINUX_STATE}" = 'Disabled' ] && exit 0

parse_args "$@"
[ -z "${VOL}" ] && exit 1

set_brick_labels "${VOL}"

exit 0
