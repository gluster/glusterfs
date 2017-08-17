#!/bin/bash

PACKAGES="automake libtool gperftools-devel gperftools-debuginfo gperftools-libs \
          glib2-devel jemalloc jemalloc-devel fb-gcc flex bison openssl-devel libxml2-devel\
          libacl-devel userspace-rcu-devel lvm2 python-devel"

if [ $(/usr/lib/rpm/redhat/dist.sh --distnum) -eq "7" ]; then
  PACKAGES="$PACKAGES libtirpc libtirpc-devel-0.2.4 devtoolset-4-binutils devtoolset-4-gcc devtoolset-4-runtime"
elif [ $(/usr/lib/rpm/redhat/dist.sh --distnum) -eq "6" ]; then
  PACKAGES="$PACKAGES libfbtirpc libfbtirpc-devel libgssglue libgssglue-devel devtoolset-2-binutils devtoolset-2-gcc devtoolset-2-runtime"
else
  echo "Centos $(/usr/lib/rpm/redhat/dist.sh --distnum) is not currently supported"
  exit 1
fi

# Skip this for Jekins automated builds (they have these packages already)
# as the sudo will cause the build to fail
[ $USER == "svcscm" ] || sudo yum install $PACKAGES -y

source ./build_env

./autogen.sh || exit 1
./configure $GF_CONF_OPTS
make -j || exit 1
