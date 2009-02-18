#!/bin/sh

aclocal
autoheader
(libtoolize --automake --copy --force || glibtoolize --automake --copy --force)
autoconf
automake --add-missing --copy --foreign
cd argp-standalone;./autogen.sh
