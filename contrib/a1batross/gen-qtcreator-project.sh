#!/bin/bash

FILES="*.c *.cpp *.h *.rc *.m *.py wscript"

rm xash3d.* && touch xash3d.files

for i in $FILES; do
	find -name "$i" >> xash3d.files
done

ln -s contrib/a1batross/xash3d.* .
