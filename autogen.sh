#!/bin/sh
#
# autogen.sh glue for wmpinboard
# $Id: autogen.sh,v 1.3 2001/07/03 02:14:16 hmh Exp $
#
set -e

# The idea is that we make sure we're always using an up-to-date
# version of all the auto* script chain for the build. The GNU autotools
# are rather badly designed in that area.

aclocal
autoheader

#we don't use symlinks because of debian's build system,
#but they would be a better choice.
[ -r /usr/share/automake/missing ] && cp -f /usr/share/automake/missing .
[ -r /usr/share/automake/install-sh ] && cp -f /usr/share/automake/install-sh .

automake
autoconf
