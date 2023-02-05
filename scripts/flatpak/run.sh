#!/bin/sh

echo "Xash3D FWGS installed as Flatpak."

export XASH3D_BASEDIR="$HOME/.xash/"
mkdir -p $XASH3D_BASEDIR
cd $XASH3D_BASEDIR
echo "Base directory is $XASH3D_BASEDIR"

# TODO: detect by libraryfolders.vdf and installed apps
HALFLIFESTEAMDIRS="../.local/share/Steam/steamapps/common/Half-Life ../.steam/steam/steamapps/common/Half-Life"

for i in $HALFLIFESTEAMDIRS; do
#	echo $i
	if [ -d "$i" ]; then
		echo "Detected Half-Life installation in $i, using as RoDir"
		export XASH3D_RODIR=$i
		break
	fi
done


export XASH3D_EXTRAS_PAK1=/app/share/xash3d/valve/extras.pk3
exec $DEBUGGER /app/lib32/xash3d/xash3d "$@"
