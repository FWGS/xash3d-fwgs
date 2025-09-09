#!/bin/sh

die()
{
        echo "$@"
        exit 1
}

echo "Xash3D FWGS installed as Flatpak."

# https://specifications.freedesktop.org/basedir-spec/basedir-spec-latest.html
# $XDG_DATA_HOME defines the base directory relative to which user-specific data files should be stored.
# If $XDG_DATA_HOME is either not set or empty, a default equal to $HOME/.local/share should be used.
if [ -z "$XDG_DATA_HOME" ]; then
        export XDG_DATA_HOME="$HOME/.local/share"
fi

if [ -z "$XASH3D_BASEDIR" ]; then
        export XASH3D_BASEDIR="$XDG_DATA_HOME/xash3d-fwgs/"
fi

mkdir -p "$XASH3D_BASEDIR"
cd "$XASH3D_BASEDIR" || die "Can't cd into $XASH3D_BASEDIR"
echo "XASH3D_BASEDIR is $XASH3D_BASEDIR"

if [ -z "$XASH3D_RODIR" ]; then
        # TODO: detect by libraryfolders.vdf and installed apps
        STEAMDIRS="\
                $HOME/.local/share/Steam/steamapps/common \
                $HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common \
                $HOME/.steam/steam/steamapps/common"
        HALFLIFEDIR="Half-Life"

        for i in $STEAMDIRS; do
                if [ ! -d "$i" ]; then
                        continue
                fi

                echo "Detected Steam library in $i, probing Half-Life..."

                if [ ! -d "$i/$HALFLIFEDIR" ]; then
                        continue
                fi

                echo "Detected Half-Life installation in $i/$HALFLIFEDIR..."

                export XASH3D_RODIR="$i/$HALFLIFEDIR"
                break
        done
fi
echo "XASH3D_RODIR is $XASH3D_RODIR"

if [ -z "$XASH3D_EXTRAS_PAK1" ]; then
        export XASH3D_EXTRAS_PAK1=/app/share/xash3d/valve/extras.pk3
fi
echo "XASH3D_EXTRAS_PAK1 is $XASH3D_EXTRAS_PAK1"

exec $DEBUGGER /app/lib32/xash3d/xash3d "$@"
