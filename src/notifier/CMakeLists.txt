# SPDX-License-Identifier: GPL-2.0-only
# SPDX-FileCopyrightText: 2026 Adam Girardo <adamjohngirardo@gmail.com>

add_executable(twu-ctl-notify main.cpp)

target_compile_features(twu-ctl-notify PRIVATE cxx_std_17)

install(TARGETS twu-ctl-notify
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
