#!/bin/sh
out1=$2
shift 2
out=$1
shift 1

echo $* > $out
rm -r $out.files
mkdir $out.files
cp --parents $* $out.files/
echo cp --parents $* $out.files/

#exit 0