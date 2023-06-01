#!/bin/bash

. scripts/lib.sh

download_sign_key()
{
	echo -n "Downloading $1: "
	curl https://community.omprussia.ru/documentation/files/doc/$1 -o $1 &> /dev/null || die "Can't download $1" && echo "OK"
}

pack_sources()
{
	echo "Packing sources to $1..."
	git ls-files --recurse-submodules | tar caf "$1" -T-
}

dependencies="SDL2-devel ImageMagick"
build_dir="build_rpm"

if [ "$1" == "aurora" ]; then
	name="su.xash.Engine"
	flavor="aurora"
else
	name="harbour-xash3d-fwgs"
	flavor="sailfish"
fi

if [ $flavor == 'aurora' ]; then
	download_sign_key regular_key.pem
	download_sign_key regular_cert.pem
fi

rm -fr ${build_dir}/{BUILD,SRPMS}
mkdir -p ${build_dir}/SOURCES
pack_sources ${build_dir}/SOURCES/${name}.tar
git clone https://github.com/FWGS/hlsdk-portable -b mobile_hacks
pushd hlsdk-portable
pack_sources ../${build_dir}/SOURCES/hlsdk-portable.tar
popd

sfdk_targets=`sfdk engine exec sb2-config -l | grep default | grep -v i486`

for each in $sfdk_targets; do
	target_arch=${each##*-}
	target_arch=${target_arch/.default/}
	echo "Build for '$each' target with '$target_arch' architecture"

	# install deps for current target
	sfdk engine exec sb2 -t $each -R -m sdk-install zypper in -y ${dependencies}

	# build RPM for current target
	sfdk engine exec sb2 -t $each rpmbuild \
		--define "_topdir `pwd`/${build_dir}" \
		--define "_arch $target_arch" \
		--define "_packagename $name" \
		--define "_flavor $flavor" \
		-ba scripts/sailfish/harbour-xash3d-fwgs.spec || die "Build for ${each}: FAIL"

	# sign RPM packacge
	if [ "$flavor" == "aurora" ]; then
		echo -n "Signing RPMs: "
		sfdk engine exec sb2 -t $each rpmsign-external sign --key `pwd`/regular_key.pem --cert `pwd`/regular_cert.pem `pwd`/${build_dir}/RPMS/${target_arch}/$name-0*.rpm || die "FAIL" && echo "OK"
	fi

	echo -n "Validate RPM: "
	if [ "$flavor" == "aurora" ]; then
		validator_output=`sfdk engine exec rpm-validator -p regular $(pwd)/${build_dir}/RPMS/${target_arch}/$name-0* 2>&1`
	else
		sfdk config target=${each/.default/}
		validator_output=`sfdk check $(pwd)/${build_dir}/RPMS/${target_arch}/$name-0* 2>&1`
	fi
	if [ $? -ne 0 ] ; then
		echo "FAIL"
		echo "${validator_output}"
		break;
	fi
	echo "OK"
done
echo "All build done! All your packages in `pwd`/build_rpm/RPMS"
