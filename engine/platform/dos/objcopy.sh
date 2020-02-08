#!/bin/sh
cd $3.files
objxdef $(cat $3) |grep -v _lib_> syms.tmp
objchg -m=$2_* -s=syms.tmp $(cat $3)
wlib $3.a $(cat $3)
cp $3.a $4
cd ..