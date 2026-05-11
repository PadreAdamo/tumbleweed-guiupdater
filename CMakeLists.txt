cmake_minimum_required(VERSION 3.22)

project(tumbleweed-updater VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# KDE build helpers
find_package(ECM REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(ECMInstallIcons)

add_subdirectory(src/gui)
add_subdirectory(src/controller)
add_subdirectory(src/notifier)

# Polkit policy — allows pkexec to run twu-ctl apply as root
install(FILES polkit/org.padreadamo.tumbleweedupdater.policy
        DESTINATION ${CMAKE_INSTALL_DATADIR}/polkit-1/actions)

# ECM parses the sc-apps- prefix to determine hicolor/scalable/apps/ destination;
# the installed filename is tumbleweed-updater.svg (prefix stripped by ECM).
ecm_install_icons(ICONS icons/sc-apps-tumbleweed-updater.svg
                  DESTINATION ${KDE_INSTALL_ICONDIR}
                  THEME hicolor)

# Systemd user units for background update checks
install(FILES
    systemd/tumbleweed-updater-check.service
    systemd/tumbleweed-updater-check.timer
    DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/user
)

# Man pages
install(FILES
    docs/tumbleweed-updater.1
    docs/twu-ctl.1
    docs/twu-ctl-notify.1
    DESTINATION ${CMAKE_INSTALL_MANDIR}/man1
)

# AppStream metadata
install(FILES
    appdata/org.padreadamo.tumbleweedupdater.appdata.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo
)
