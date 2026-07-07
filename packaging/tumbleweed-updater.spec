# To build locally: rpmbuild -ba packaging/tumbleweed-updater.spec
#
# Source tarball naming must match: tumbleweed-updater-%%{version}.tar.gz
# On OBS, use <scm> or upload a tarball with that name.

Name:           tumbleweed-updater
Version:        0.1.4
Release:        0
Summary:        KDE-native GUI system updater for openSUSE Tumbleweed
License:        GPL-2.0-only
Group:          System/GUI/KDE
URL:            https://github.com/PadreAdamo/tumbleweed-guiupdater
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake >= 3.22
BuildRequires:  extra-cmake-modules
BuildRequires:  pkgconfig(Qt6Core)
BuildRequires:  pkgconfig(Qt6Gui)
BuildRequires:  pkgconfig(Qt6Qml)
BuildRequires:  pkgconfig(Qt6Quick)
BuildRequires:  pkgconfig(Qt6QuickControls2)
BuildRequires:  pkgconfig(Qt6Widgets)
BuildRequires:  pkgconfig(Qt6DBus)
BuildRequires:  kf6-kirigami-devel
BuildRequires:  kf6-knotifications-devel
BuildRequires:  kf6-kconfig-devel
BuildRequires:  kf6-kstatusnotifieritem-devel
# polkit-devel provides the pkexec policy validation tool used at install time
BuildRequires:  polkit-devel
BuildRequires:  desktop-file-utils
BuildRequires:  systemd-rpm-macros
BuildRequires:  appstream-glib

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
# fwupd is optional; firmware update detection/apply is enabled by default
# when fwupdmgr is present and silently skipped if it is absent
Recommends:     fwupd

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
appstream-util validate-relax --nonet appdata/org.padreadamo.tumbleweedupdater.appdata.xml

%install
%cmake_install

%post
%systemd_user_post tumbleweed-updater-check.timer

%preun
%systemd_user_preun tumbleweed-updater-check.timer

%postun
%systemd_user_postun tumbleweed-updater-check.timer

%files
%license LICENSE
%doc README.md
%{_bindir}/tumbleweed-updater
%{_bindir}/twu-ctl
%{_bindir}/twu-ctl-notify
%{_userunitdir}/tumbleweed-updater-check.service
%{_userunitdir}/tumbleweed-updater-check.timer
%{_datadir}/knotifications6/TumbleweedUpdater.notifyrc
%{_datadir}/polkit-1/actions/org.padreadamo.tumbleweedupdater.policy
%{_datadir}/applications/tumbleweed-updater.desktop
%{_datadir}/icons/hicolor/scalable/apps/tumbleweed-updater.svg
%{_mandir}/man1/tumbleweed-updater.1%{?ext_man}
%{_mandir}/man1/twu-ctl.1%{?ext_man}
%{_mandir}/man1/twu-ctl-notify.1%{?ext_man}
%{_datadir}/metainfo/org.padreadamo.tumbleweedupdater.appdata.xml

%changelog
* Mon Jul 06 2026 Adam Girardo <adamjohngirardo@gmail.com> - 0.1.4-0
- Add fwupd firmware update detection: cmd_status() checks fwupdmgr for
  pending firmware updates alongside zypper and Flatpak, gated by a
  [Fwupd] Enabled setting (default on if fwupdmgr is installed)
- Bundle firmware apply into the existing Apply Updates flow — applied
  together with zypper/Flatpak updates rather than a separate button,
  and always marks a reboot as required
- Add Settings toggle for fwupd firmware updates, hidden entirely when
  fwupdmgr is not installed
- Add Recommends: fwupd to the RPM spec

* Mon May 11 2026 Adam Girardo <adamjohngirardo@gmail.com> - 0.1.3-0
- Add Flatpak update detection: cmd_status() checks for Flatpak updates and
  reports them separately in the status JSON and GUI status text
- Fix Flatpak detection: trim_copy now strips newlines from fgets output
- Fix "Find Snapper Tools" button: package snapper-gui does not exist in
  Tumbleweed repos; open openSUSE software search in browser instead
- Add systemd user timer (tumbleweed-updater-check.timer) and
  twu-ctl-notify background notification helper binary
- Add first-launch dialog offering to enable the systemd timer
- Add Settings toggle to enable/disable the systemd timer directly
- Show "Next check: in X hours Y minutes" in Settings when timer is active
- Sync timer interval via drop-in override file on interval change
- Fix default check interval: was 4h throughout, now consistently 24h
- Show History tab entries newest-first
- Add man pages for tumbleweed-updater(1), twu-ctl(1), twu-ctl-notify(1)
- Add AppStream metadata (org.padreadamo.tumbleweedupdater.appdata.xml)
- Remove debug logging from GUI auto-check timer callback

* Sun May 10 2026 Adam Girardo <adamjohngirardo@gmail.com> - 0.1.2-0
- Fix stale repo cache: run zypper ref via pkexec before status checks
- Fix history log path mismatch (doubled org/app name in XDG path)
- Reload History tab when returning from Settings page
- Add polkit refresh action with auth_admin_keep for background checks
- Make gear icon toggle Settings page open/closed

* Thu May 07 2026 Adam Girardo <adamjohngirardo@gmail.com> - 0.1.0-0
- Initial packaging for OBS submission
