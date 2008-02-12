#!/bin/sh

aclocal
autoheader
libtoolize --automake --copy --force
autoconf
automake --add-missing --copy --foreign
