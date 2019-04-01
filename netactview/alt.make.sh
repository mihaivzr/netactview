#!/bin/sh

# ./autogen.sh
# ./configure 
aclocal
libtoolize --force  
autoheader  
autoconf
./configure 

# ./configure --disable-dependency-tracking
# ou
ln -s /usr/share/automake-1.15/depcomp

make
