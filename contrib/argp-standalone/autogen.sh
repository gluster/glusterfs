#!/bin/sh

aclocal -I .
autoheader
autoconf
automake --add-missing --copy --foreign
