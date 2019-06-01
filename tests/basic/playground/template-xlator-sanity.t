#!/bin/bash

. $(dirname $0)/../../include.rc
. $(dirname $0)/../../volume.rc

cleanup;

TEST mkdir -p $B0/single-brick
cat > $B0/template.vol <<EOF
volume posix
  type storage/posix
  option directory $B0/single-brick
end-volume

volume template
  type playground/template
  subvolumes posix
  option dummy 13
end-volume
EOF

TEST glusterfs -f $B0/template.vol $M0

TEST $(dirname $0)/../rpc-coverage.sh --no-locks $M0

# Take statedump to get maximum code coverage
pid=$(ps auxww | grep glusterfs | grep -E "template.vol" | awk '{print $2}' | head -1)

TEST generate_statedump $pid

# For monitor output
kill -USR2 $pid

# Handle SIGHUP and reconfigure
sed -i -e '/s/dummy 13/dummy 42/g' $B0/template.vol
kill -HUP $pid

# for calling 'fini()'
kill -TERM $pid

force_umount $M0

cleanup;
