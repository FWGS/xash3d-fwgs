#!/bin/bash

# Was used to move bunch of travis autobuilds to new path

# Remove unneeded info
function clearName
{
	echo $1 | sed 's/xashdroid-//;s/.apk//'
}

function getYear
{
	echo $1 | awk -F- '{ print $(NF-4)}'
}

function getMonth
{
	echo $1 | awk -F- '{ print $(NF-3)}'
}

function getDay
{
	echo $1 | awk -F- '{ print $(NF-2)}' | awk -F_ '{ print $1 }'
}

function getHour
{
	echo $1 | awk -F- '{ print $(NF-2)}' | awk -F_ '{ print $2 }'
}

function getMinute
{
	echo $1 | awk -F- '{ print $(NF-1)}'
}

function getHash
{
	echo $1 | awk -F- '{ print $(NF)}'
}


function getArch
{
	local SECOND=`echo $1 | awk -F- '{ print $(NF-5)}'`
	local FIRST=`echo $1 | awk -F- '{ print $(NF-6)}'`
	
	if [ "$SECOND" = "tegra2" ]; then
		echo $FIRST-$SECOND
	else
		echo $SECOND
	fi
}

function getDaysSinceRelease
{
	printf %04d $(( ( $(date -ud "$1$2$3" +'%s') - $(date -ud '150401' +'%s') )/60/60/24 ))
}


for i in *.apk;
do
	NAME=`clearName ${i}`
	YEAR=`getYear ${NAME}`
	MONTH=`getMonth ${NAME}`
	DAY=`getDay ${NAME}`
	HOUR=`getHour ${NAME}`
	MINUTE=`getMinute ${NAME}`
	ARCH=`getArch ${NAME}`
	HASH=`getHash ${NAME}`
	DAYSSINCERELEASE=`getDaysSinceRelease ${YEAR} ${MONTH} ${DAY}`
	
	echo "Moving ${i} to 20${YEAR}/${MONTH}/${DAY}/xash3d-${DAYSSINCERELEASE}-${HOUR}-${MINUTE}-${ARCH}-${HASH}.apk"
	mkdir -p 20${YEAR}/${MONTH}/${DAY}/
	mv ${i} 20${YEAR}/${MONTH}/${DAY}/xash3d-${DAYSSINCERELEASE}-${HOUR}-${MINUTE}-${ARCH}-${HASH}.apk
done