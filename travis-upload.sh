\#!/bin/sh

# Upload travis generated APKs to the Transfer.sh and Yandex.Disk

getDaysSinceRelease()
{
	printf %04d $(( ( $(date +'%s') - $(date -ud '2015-04-01' +'%s') )/60/60/24 ))
}

DAYSSINCERELEASE=`getDaysSinceRelease`
COMMITHASH=$(git rev-parse --short HEAD)
#CURRENTBRANCH=$(git rev-parse --abbrev-ref HEAD)


PREFIX=$DAYSSINCERELEASE-$(date +"%H-%M")
POSTFIX=$COMMITHASH

while [ "$1" != "" ]; do
FNAME=$1
FILE_BASE=${FNAME%.*}
FILE_EXT="${FNAME##*.}"
OUTNAME=$PREFIX-$FILE_BASE-$POSTFIX.$FILE_EXT
echo $FNAME: `curl --upload-file $FNAME https://transfer.sh/$OUTNAME`
curl -T $FNAME -u $YADISK_USERNAME:$YADISK_PASSWORD https://webdav.yandex.ru/XashTestVersions/$YADISKPATH/$OUTNAME > /dev/null
curl -T xashdroid-armv7.apk -u $YADISK_USERNAME:$YADISK_PASSWORD https://webdav.yandex.ru/XashTestVersions/current-$FILE_BASE-$TRAVIS_BRANCH.$FILE_EXT > /dev/null
shift
done
exit 0
