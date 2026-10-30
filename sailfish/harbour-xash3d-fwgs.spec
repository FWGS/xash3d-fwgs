# See Documentation/sailfish.md for build instructions

# the build number the engine reports, days from Apr 1 2015 to the commit date, so rpmbuild must run in the git checkout
%{!?buildnum:%global buildnum %(d=$(git log -1 --format=%%cs 2>/dev/null) && echo $(( ( $(date -u -d "$d 00:00:00" +%%s) - $(date -u -d "2015-04-01 00:00:00" +%%s) ) / 86400 )) || echo 0)}

Name: harbour-xash3d-fwgs
Summary: Xash3D FWGS
Version: 0.21
Release: %{buildnum}
Group: Amusements/Games
License: GPLv3+
URL: https://github.com/FWGS/xash3d-fwgs
Source0: %{name}-%{version}.tar.gz
Source1: hlsdk-portable.tar.gz
BuildRequires: python3-base
BuildRequires: git
BuildRequires: pkgconfig(sdl2)
BuildRequires: pkgconfig(gio-2.0)
BuildRequires: pkgconfig(audioresource)
BuildRequires: pkgconfig(wayland-client)
BuildRequires: zlib-devel
BuildRequires: bzip2-devel
BuildRequires: libogg-devel
BuildRequires: libvorbis-devel
BuildRequires: pkgconfig(sailfishapp)
BuildRequires: pkgconfig(Qt5Core)
BuildRequires: pkgconfig(Qt5Gui)
BuildRequires: pkgconfig(Qt5Network)
BuildRequires: pkgconfig(Qt5Qml)
BuildRequires: pkgconfig(Qt5Quick)
BuildRequires: qt5-qttools-linguist
# the default renderer, nanogl, loads it through SDL, so it's not picked up automatically
%ifarch aarch64
Requires: libGLESv1_CM.so.1()(64bit)
%else
Requires: libGLESv1_CM.so.1
%endif

# keep debug info inside the binaries, the engine crash handler needs it for backtraces
%define debug_package %{nil}
%define __strip /bin/true

# Harbour doesn't allow to provide or require libraries we ship ourselves
%define __provides_exclude_from ^%{_datadir}/.*$
%define __requires_exclude ^(libxash|libmenu|libref_.*|filesystem_stdio|client_.*|hl_.*)\\.so.*$

%description
Xash3D FWGS is a game engine compatible with Half-Life and its mods.

%prep
%setup -q -c
mkdir hlsdk-portable
tar -xf %{SOURCE1} -C hlsdk-portable

%build
if command -v ccache > /dev/null; then
	export CC="ccache gcc" CXX="ccache g++"
fi
python3 waf configure -T release --sailfish --enable-packaging --prefix=%{_prefix}
python3 waf build %{?_smp_mflags}
cd hlsdk-portable
python3 waf configure -T release
python3 waf build %{?_smp_mflags}

%install
python3 waf install --destdir=%{buildroot}
install -D -m 644 hlsdk-portable/LICENSE %{buildroot}%{_datadir}/%{name}/licenses/hlsdk-portable.txt
cd hlsdk-portable
python3 waf install --destdir=%{buildroot}%{_datadir}/%{name}/lib/rodir

%files
%defattr(-,root,root,-)
%{_bindir}/%{name}
%{_datadir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/*/apps/%{name}.png

%changelog
* Thu Jun 1 2023 a1batross <a1ba.omarov@gmail.com>
- initial port
