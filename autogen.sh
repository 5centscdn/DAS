#!/bin/sh
aclocal -I m4
libtoolize --copy --force
automake --add-missing --copy --foreign
autoconf

cd lib/json-c && autoreconf -v --install || exit 1
