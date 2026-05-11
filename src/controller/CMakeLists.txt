# SPDX-License-Identifier: GPL-2.0-only
# SPDX-FileCopyrightText: 2026 Adam Girardo <adamjohngirardo@gmail.com>

add_executable(twu-ctl main.cpp)

install(TARGETS twu-ctl
  RUNTIME DESTINATION bin
)
