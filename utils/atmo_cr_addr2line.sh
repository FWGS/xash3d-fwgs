#!/bin/bash

MODULE_DIR=dist

IFS=''
while read -r line; do
	echo -n $line

	MODULE_AND_ADDRESS=$(echo $line | grep -oE "[0-9A-z]+\ \+\ 0x[0-9a-z]+")

	if [ ! -z "$MODULE_AND_ADDRESS" ]; then
		MODULE=$(echo $MODULE_AND_ADDRESS | cut -d' ' -f1 )
		ADDR=$(echo $MODULE_AND_ADDRESS | cut -d' ' -f3 )

		if [ ! -e $MODULE_DIR/$MODULE ]; then
			MODULE=$MODULE".so"
		fi

		echo -e "\t"$(addr2line -e $MODULE_DIR/$MODULE $ADDR)
	else
		echo
	fi
done
