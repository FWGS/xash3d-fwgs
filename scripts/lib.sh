die()
{
	cat build/config.log
	exit 1
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
