die()
{
	exit 1
}

die_configure()
{
	cat build/config.log
	die
}

get_ffmpeg_archive()
{
	if [ "$GH_CPU_OS" == "win32" ]; then
		A=win
	else
		A="$GH_CPU_OS"
	fi

	if [ "$GH_CPU_ARCH" == "amd64" ]; then
		B=64
	elif [ "$GH_CPU_ARCH" == "i386" ]; then
		B=32
	else
		B="$GH_CPU_ARCH"
	fi

	FLAVOR=lgpl-shared-minimal

	echo "ffmpeg-n$FFMPEG_VERSION-latest-$A$B-$FLAVOR-$FFMPEG_VERSION"
}

if [ -n "$TRAVIS_BUILD_DIR" ]; then
	BUILDDIR=$TRAVIS_BUILD_DIR
elif [ -n "$GITHUB_WORKSPACE" ]; then
	BUILDDIR=$GITHUB_WORKSPACE
fi

if [ -n "$TRAVIS_CPU_ARCH" ]; then
	ARCH=$TRAVIS_CPU_ARCH
elif [ -n "$GH_CPU_ARCH" ]; then
	ARCH=$GH_CPU_ARCH
fi
