#!/bin/sh
if [ -d /lib/modules/`uname -r`/build ] ; then
    echo Building KM $1 -
    make -C /lib/modules/`uname -r`/build M=`pwd` $1
fi
