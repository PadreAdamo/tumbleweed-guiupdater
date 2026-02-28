find_package(PkgConfig REQUIRED)

pkg_check_modules(QT6CORE REQUIRED IMPORTED_TARGET Qt6Core)
pkg_check_modules(QT6GUI REQUIRED IMPORTED_TARGET Qt6Gui)
pkg_check_modules(QT6QML REQUIRED IMPORTED_TARGET Qt6Qml)
pkg_check_modules(QT6QUICK REQUIRED IMPORTED_TARGET Qt6Quick)
pkg_check_modules(QT6QC2 REQUIRED IMPORTED_TARGET Qt6QuickControls2)

add_executable(tumbleweed-updater
    main.cpp
)

target_compile_definitions(tumbleweed-updater PRIVATE
    TWU_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}\"
)

target_link_libraries(tumbleweed-updater PRIVATE
    PkgConfig::QT6CORE
    PkgConfig::QT6GUI
    PkgConfig::QT6QML
    PkgConfig::QT6QUICK
    PkgConfig::QT6QC2
)
