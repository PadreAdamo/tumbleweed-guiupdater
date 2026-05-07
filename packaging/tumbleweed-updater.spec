# To build locally: rpmbuild -ba packaging/tumbleweed-updater.spec
#
# Source tarball naming must match: tumbleweed-updater-%%{version}.tar.gz
# On OBS, use <scm> or upload a tarball with that name.

Name:           tumbleweed-updater
Version:        0.1.0
Release:        0
Summary:        KDE-native GUI system updater for openSUSE Tumbleweed
License:        GPL-2.0-only
Group:          System/GUI/KDE
URL:            https://github.com/padreadamo/tumbleweed-guiupdater
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake >= 3.22
BuildRequires:  extra-cmake-modules
BuildRequires:  pkgconfig(Qt6Core)
BuildRequires:  pkgconfig(Qt6Gui)
BuildRequires:  pkgconfig(Qt6Qml)
BuildRequires:  pkgconfig(Qt6Quick)
BuildRequires:  pkgconfig(Qt6QuickControls2)
BuildRequires:  pkgconfig(Qt6DBus)
BuildRequires:  kf6-kirigami-devel
BuildRequires:  kf6-knotifications-devel
BuildRequires:  kf6-kconfig-devel
BuildRequires:  kf6-kstatusnotifieritem-devel
# polkit-devel provides the pkexec policy validation tool used at install time
BuildRequires:  polkit-devel
BuildRequires:  desktop-file-utils

Requires:       zypper
Requires:       polkit
Requires:       qt6-quick
Requires:       kf6-kirigami
Requires:       kf6-knotifications
Requires:       kf6-kconfig
# snapper is optional; the GUI skips snapshot creation if it is absent
Recommends:     snapper
# flatpak updates are off by default and require the flatpak CLI at runtime
Suggests:       flatpak

%description
Tumbleweed Updater is a KDE Plasma system tray application that keeps your
openSUSE Tumbleweed installation current without requiring a terminal. It
runs zypper dup on your behalf, streams live output, and takes Snapper
pre/post snapshots so you always have a rollback point. Privilege separation
is enforced: the GUI never runs as root — only the small controller binary
(twu-ctl) is elevated via pkexec when applying updates.

%prep
%autosetup

%build
%cmake -DCMAKE_BUILD_TYPE=Release
%cmake_build

%check
# No automated test suite yet.

%install
%cmake_install

%files
%license LICENSE
%doc README.md
%{_bindir}/tumbleweed-updater
%{_bindir}/twu-ctl
%{_datadir}/knotifications6/TumbleweedUpdater.notifyrc
%{_datadir}/polkit-1/actions/org.padreadamo.tumbleweedupdater.policy
%{_datadir}/applications/tumbleweed-updater.desktop
# %%{_datadir}/icons/hicolor/scalable/apps/tumbleweed-updater.svg

%changelog
* Thu May 07 2026 Adam Girardo <adamjohngirardo@gmail.com> - 0.1.0-0
- Initial packaging for OBS submission
