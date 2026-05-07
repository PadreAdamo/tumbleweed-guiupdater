find_package(PkgConfig REQUIRED)

pkg_check_modules(QT6CORE REQUIRED IMPORTED_TARGET Qt6Core)
pkg_check_modules(QT6GUI REQUIRED IMPORTED_TARGET Qt6Gui)
pkg_check_modules(QT6QML REQUIRED IMPORTED_TARGET Qt6Qml)
pkg_check_modules(QT6QUICK REQUIRED IMPORTED_TARGET Qt6Quick)
pkg_check_modules(QT6QC2 REQUIRED IMPORTED_TARGET Qt6QuickControls2)
pkg_check_modules(QT6WIDGETS REQUIRED IMPORTED_TARGET Qt6Widgets)

find_program(Qt6_RCC NAMES rcc HINTS /usr/libexec/qt6 /usr/lib64/qt6/bin /usr/lib/qt6/bin REQUIRED)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/qml_resources.cpp
    COMMAND ${Qt6_RCC}
            --name qml_resources
            --output ${CMAKE_CURRENT_BINARY_DIR}/qml_resources.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/resources.qrc
    DEPENDS
            ${CMAKE_CURRENT_SOURCE_DIR}/resources.qrc
            ${CMAKE_CURRENT_SOURCE_DIR}/qml/Main.qml
    VERBATIM
)

add_executable(tumbleweed-updater
    main.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/qml_resources.cpp
)

target_link_libraries(tumbleweed-updater PRIVATE
    PkgConfig::QT6CORE
    PkgConfig::QT6GUI
    PkgConfig::QT6QML
    PkgConfig::QT6QUICK
    PkgConfig::QT6QC2
    PkgConfig::QT6WIDGETS
)
