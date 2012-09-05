#!/bin/bash

process_file()
{
    dst=$1/$(basename $2)
    src=$2
    if [ $dst == $src ]; then
      cp -f -p $src $src.tmp
      src=$2.tmp
    else
      cp -f -p $src $dst
    fi

    sed "s|/system/bin/linker\x0|/sbin/linker\x0\x0\x0\x0\x0\x0\x0|g" $src | sed "s|/system/bin/sh\x0|/sbin/sh\x0\x0\x0\x0\x0\x0\x0|g" > $dst

    if [ $1 == $(dirname $2) ]; then
      rm -f $src
    fi
}


dest=$1
shift 1
for ARG in $*
do
    process_file $dest $ARG
done
