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

function delete_brick_fcontext()
{
  local volname=$1
  local fctx
  local list=()

  fctx="$(semanage fcontext --list -C)"
  # grab the path for each local brick
  brickpath="/var/lib/glusterd/vols/${volname}/bricks/"
  brickdirs=$(find "${brickpath}" -type f -exec grep '^path=' {} \; | \
    cut -d= -f 2 | sort -u)
  for b in ${brickdirs}
  do
    pattern="${b}(/.*)?"
    echo "${fctx}" | grep "^${pattern}\s" >/dev/null
    if [[ $? -eq 0 ]]; then
      list+=("${pattern}")
    fi
  done
  if [[ ${#list[@]} -gt 0 ]]; then
    printf 'fcontext --delete %s\n' "${list[@]}" | semanage -i -
  fi
  for b in ${brickdirs}
  do
    restorecon -R "${b}"
  done
}

SELINUX_STATE=$(which getenforce && getenforce)
[ "${SELINUX_STATE}" = 'Disabled' ] && exit 0

parse_args "$@"
[ -z "${VOL}" ] && exit 1

delete_brick_fcontext "${VOL}"

# failure to delete the fcontext is not fatal
exit 0
