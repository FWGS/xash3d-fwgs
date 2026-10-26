#!/bin/bash

# Builds the RPM on a Sailfish OS device, run from the repository root
# usage: scripts/sailfish/build.sh [ssh host]
# without arguments it must be run on the device itself,
# otherwise sources are packed here and the device builds them over ssh

. scripts/lib.sh

name=harbour-xash3d-fwgs
version=$(sed -n 's/^Version:[[:space:]]*//p' sailfish/$name.spec)
cache=build/sailfish
remote=sailfish-rpmbuild
host=$1

pack_tree()
{
	# untracked files are packed too, so changes can be tested before committing them
	# no top directory in the archive, busybox tar on the device can't add it
	{ git ls-files --recurse-submodules; git ls-files --others --exclude-standard; } | sort -u | while IFS= read -r f; do
		# skip files deleted but not yet committed
		[ -e "$f" ] || [ -L "$f" ] && printf '%s\n' "$f"
	done | tar czf "$1" -T - || die
}

mkdir -p $cache/SOURCES

if [ ! -d $cache/hlsdk-portable ]; then
	git clone --depth 1 --recursive -b mobile_hacks https://github.com/FWGS/hlsdk-portable $cache/hlsdk-portable || die
fi

pack_tree $cache/SOURCES/$name-$version.tar.gz
(cd $cache/hlsdk-portable && pack_tree ../SOURCES/hlsdk-portable.tar.gz) || die

if [ -z "$host" ]; then
	rpmbuild --define "_topdir $PWD/$cache" -ba sailfish/$name.spec || die
	ls $cache/RPMS/*/$name-*.rpm
	exit 0
fi

ssh $SAILFISH_SSH_OPTS $host "rm -rf $remote && mkdir -p $remote/SOURCES" || die
scp $SAILFISH_SSH_OPTS $cache/SOURCES/$name-$version.tar.gz $cache/SOURCES/hlsdk-portable.tar.gz $host:$remote/SOURCES/ || die
scp $SAILFISH_SSH_OPTS sailfish/$name.spec $host:$remote/ || die
ssh $SAILFISH_SSH_OPTS $host "rpmbuild --define \"_topdir \$HOME/$remote\" -ba $remote/$name.spec" || die

mkdir -p $cache/RPMS
scp $SAILFISH_SSH_OPTS "$host:$remote/RPMS/*/$name-*.rpm" $cache/RPMS/ || die
ls $cache/RPMS/$name-*.rpm
