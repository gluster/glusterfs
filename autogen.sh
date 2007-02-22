#!/bin/sh

aclocal
libtoolize --automake --copy --force
autoconf
automake --add-missing --copy --foreign
