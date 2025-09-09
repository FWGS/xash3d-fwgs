# Based on harbour-quake2 spec file

Name: %{_packagename}
Summary: Xash3D FWGS
Release: 1
Version: 0.21
Group: Amusements/Games
License: GPLv2
BuildArch: %{_arch}
URL: https://github.com/FWGS/xash3d-fwgs
Source0: %{name}.tar
Source1: hlsdk-portable.tar
BuildRequires: SDL2-devel ImageMagick

%define __provides_exclude_from ^%{_datadir}/%{name}/lib/.*$

%description
Xash3D FWGS is a game engine compatible with Half-Life 1 and mods.

%prep
tar -xf %{_topdir}/SOURCES/%{name}.tar
python3 waf configure \
	-T release \
	--sailfish=%{_flavor} \
	--enable-stbtt \
	--enable-bundled-deps \
	--enable-packaging \
	--disable-gl \
	--enable-gles2 \
	--enable-gl4es \
	--prefix=/usr \
	--libdir=%{_datadir}/%{name}/lib \
	--bindir=%{_bindir}

mkdir -p hlsdk-portable
pushd hlsdk-portable
tar -xf %{_topdir}/SOURCES/hlsdk-portable.tar
python3 waf configure -T release
popd

%build
python3 waf build -j$(($(nproc)+1))
pushd hlsdk-portable
python3 waf build -j$(($(nproc)+1))
popd

%install
python3 waf install --destdir=%{buildroot}
pushd hlsdk-portable
python3 waf install --destdir=%{buildroot}%{_datadir}/%{name}/rodir
popd
# rename real binary
mv %{buildroot}/usr/bin/xash3d %{buildroot}/usr/bin/%{name}

install -d %{buildroot}/%{_datadir}/applications
sed "s/__REPLACE_ICON__/su.xash.Engine/g;s/__REPLACE_EXEC__/su.xash.Engine/g;" scripts/sailfish/harbour-xash3d-fwgs.desktop > %{buildroot}/%{_datadir}/applications/%{name}.desktop
chmod 644 %{buildroot}/%{_datadir}/applications/%{name}.desktop

install -d %{buildroot}/%{_datadir}/icons/hicolor/86x86/apps
install -d %{buildroot}/%{_datadir}/icons/hicolor/108x108/apps
install -d %{buildroot}/%{_datadir}/icons/hicolor/128x128/apps
install -d %{buildroot}/%{_datadir}/icons/hicolor/172x172/apps
convert game_launch/icon-xash-material.png -resize 86x86 %{buildroot}/%{_datadir}/icons/hicolor/86x86/apps/%{name}.png
convert game_launch/icon-xash-material.png -resize 108x108 %{buildroot}/%{_datadir}/icons/hicolor/108x108/apps/%{name}.png
convert game_launch/icon-xash-material.png -resize 128x128 %{buildroot}/%{_datadir}/icons/hicolor/128x128/apps/%{name}.png
convert game_launch/icon-xash-material.png -resize 172x172 %{buildroot}/%{_datadir}/icons/hicolor/172x172/apps/%{name}.png

%files
%defattr(-,root,root,-)
%attr(755,root,root) %{_bindir}/%{name}
%attr(755,root,root) %{_datadir}/%{name}/lib/*
%attr(644,root,root) %{_datadir}/%{name}/rodir/valve/extras.pk3
%attr(755,root,root) %{_datadir}/%{name}/rodir/valve/cl_dlls/*
%attr(755,root,root) %{_datadir}/%{name}/rodir/valve/dlls/*
%attr(644,root,root) %{_datadir}/icons/hicolor/86x86/apps/%{name}.png
%attr(644,root,root) %{_datadir}/icons/hicolor/108x108/apps/%{name}.png
%attr(644,root,root) %{_datadir}/icons/hicolor/128x128/apps/%{name}.png
%attr(644,root,root) %{_datadir}/icons/hicolor/172x172/apps/%{name}.png
%{_datadir}/applications/%{name}.desktop

%changelog
* Thu Jun 1 2023 a1batross <a1ba.omarov@gmail.com>
- initial port
