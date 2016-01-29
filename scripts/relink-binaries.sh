#!/bin/bash

process_file()
{
   src=$1
   dst=$1-1 #/$(basename $2)
   cp -f -p $src $dst

   sed "s|/system/bin/linker64\x0|/sbin/linker64\x0\x0\x0\x0\x0\x0\x0|g" $dst | sed "s|/system/bin/linker\x0|/sbin/linker\x0\x0\x0\x0\x0\x0\x0|g" | sed "s|/system/bin/sh\x0|/sbin/sh\x0\x0\x0\x0\x0\x0\x0|g" > $dst-mod
   #sed "s|/sbin/linker\x0|/system/bin/linker\x0\x0\x0\x0\x0\x0\x0|g" $dst | sed "s|/sbin/sh\x0|/system/bin/sh\x0\x0\x0\x0\x0\x0\x0|g" > $dst-mod
   rm $dst
}


dest=$1
for ARG in $*
do
   process_file $dest $ARG
done
