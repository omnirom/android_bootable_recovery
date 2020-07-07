#!/bin/bash

process_file()
{
    dst=$1/$(basename $2)
    src=$2

    [[ -e $src ]] || return 0

    if [ $dst == $src ]; then
      cp -f -p $src $src.tmp
      src=$2.tmp
    fi

    cp $src $dst
    if [[ -e $2.tmp ]]; then rm -f $2.tmp; fi
}


dest=$1
shift 1
for ARG in $*
do
    process_file $dest $ARG
done
