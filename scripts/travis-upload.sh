#!/bin/bash

# Upload travis generated APKs to the Transfer.sh and Yandex.Disk

function getDaysSinceRelease
{
	printf %04d $(( ( $(date -ud +'%s') - $(date -ud '150401' +'%s') )/60/60/24 ))
}

DAYSSINCERELEASE=`getDaysSinceRelease`
COMMITHASH=$(git rev-parse --short HEAD)
CURRENTBRANCH=$(git rev-parse --abbrev-ref HEAD)

function generateFileName
{
	echo "xash3d-$DAYSSINCERELEASE-$(date +"%H-%M")-$1-$COMMITHASH.apk"
}



# Transfer.sh
TRANSFERSH_ARMV5=`curl --upload-file xashdroid-armv5.apk https://transfer.sh/$(generateFileName armv5)`
TRANSFERSH_ARMV6=`curl --upload-file xashdroid-armv6.apk https://transfer.sh/$(generateFileName armv6)`
TRANSFERSH_ARMV7=`curl --upload-file xashdroid-armv7.apk https://transfer.sh/$(generateFileName armv7)`
TRANSFERSH_ARMV7TEGRA2=`curl --upload-file xashdroid-armv7-tegra2.apk https://transfer.sh/$(generateFileName armv7-tegra2)`
TRANSFERSH_X86=`curl --upload-file xashdroid-x86.apk https://transfer.sh/$(generateFileName x86)`

echo "Transfer.sh links:\narmv5:\t${TRANSFERSH_ARMV5}\narmv6:\t${TRANSFERSH_ARMV6}\narmv7:\t${TRANSFERSH_ARMV7}\ntegra2:\t${TRANSFERSH_ARMV7TEGRA2}\nx86:\t${TRANSFERSH_X86}\n"

# YaDisk

YADISKPATH=`date +%Y/%m/%d`

curl -u $YADISK_USERNAME:$YADISK_PASSWORD -X MKCOL https://webdav.yandex.ru/XashTestVersions/`date +%Y`
curl -u $YADISK_USERNAME:$YADISK_PASSWORD -X MKCOL https://webdav.yandex.ru/XashTestVersions/`date +%Y/%m`/
curl -u $YADISK_USERNAME:$YADISK_PASSWORD -X MKCOL https://webdav.yandex.ru/XashTestVersions/`date +%Y/%m/%d`/

curl -T xashdroid-armv7.apk -u $YADISK_USERNAME:$YADISK_PASSWORD https://webdav.yandex.ru/XashTestVersions/$YADISKPATH/$(generateFileName armv7)
curl -T xashdroid-armv6.apk -u $YADISK_USERNAME:$YADISK_PASSWORD https://webdav.yandex.ru/XashTestVersions/$YADISKPATH/$(generateFileName armv6)
curl -T xashdroid-armv5.apk -u $YADISK_USERNAME:$YADISK_PASSWORD https://webdav.yandex.ru/XashTestVersions/$YADISKPATH/$(generateFileName armv5)
curl -T xashdroid-armv7-tegra2.apk -u $YADISK_USERNAME:$YADISK_PASSWORD https://webdav.yandex.ru/XashTestVersions/$YADISKPATH/$(generateFileName armv7-tegra2)
curl -T xashdroid-x86.apk -u $YADISK_USERNAME:$YADISK_PASSWORD https://webdav.yandex.ru/XashTestVersions/$YADISKPATH/$(generateFileName x86)

# Update current
function generateFileName_current
{
	echo "xash3d-current-$1-$CURRENTBRANCH-$COMMITHASH.apk"
}

curl -T xashdroid-armv7.apk -u $YADISK_USERNAME:$YADISK_PASSWORD https://webdav.yandex.ru/XashTestVersions/$YADISKPATH/$(generateFileName_current armv7)
curl -T xashdroid-armv6.apk -u $YADISK_USERNAME:$YADISK_PASSWORD https://webdav.yandex.ru/XashTestVersions/$YADISKPATH/$(generateFileName_current armv6)
curl -T xashdroid-armv5.apk -u $YADISK_USERNAME:$YADISK_PASSWORD https://webdav.yandex.ru/XashTestVersions/$YADISKPATH/$(generateFileName_current armv5)
curl -T xashdroid-armv7-tegra2.apk -u $YADISK_USERNAME:$YADISK_PASSWORD https://webdav.yandex.ru/XashTestVersions/$YADISKPATH/$(generateFileName_current armv7-tegra2)
curl -T xashdroid-x86.apk -u $YADISK_USERNAME:$YADISK_PASSWORD https://webdav.yandex.ru/XashTestVersions/$YADISKPATH/$(generateFileName_current x86)


exit 0