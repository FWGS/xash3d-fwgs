#!/bin/sh

echo "Xash3D FWGS installed as Flatpak."

# TODO: detect by libraryfolders.vdf and installed apps
HALFLIFESTEAMDIRS="$HOME/.local/share/Steam/steamapps/common/Half-Life $HOME/.steam/steam/steamapps/common/Half-Life"

for i in $HALFLIFESTEAMDIRS; do
#	echo $i
	if [ -d "$i" ]; then
		echo "Detected Half-Life installation in $i, using as RoDir"
		export XASH3D_RODIR=$i
		break
	fi
done

XASHDATADIR="$HOME/.xash/"

mkdir -p $XASHDATADIR
export XASH3D_BASEDIR="$XASHDATADIR"
echo "Base directory is $XASH3D_BASEDIR"

export XASH3D_EXTRAS_PAK1=/app/share/xash3d/valve/extras.pk3
exec $DEBUGGER /app/lib32/xash3d/xash3d "$@"
