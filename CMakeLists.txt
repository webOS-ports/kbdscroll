# Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
#
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.10)
project(kbdscroll C)

include(GNUInstallDirs)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)
add_compile_options(-Wall -Wextra)

# One file, libc only: evdev and uinput are kernel headers.
add_executable(kbdscroll kbdscroll.c)
install(TARGETS kbdscroll DESTINATION ${CMAKE_INSTALL_SBINDIR})

# The distro default, every setting commented out, and the drop-in directory
# per-machine settings go to (10-<machine>.conf, 90-local.conf).
install(FILES kbdscroll.conf DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR})
install(DIRECTORY DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}/kbdscroll.conf.d)

# Device profiles, installed by codename and picked at run time by the
# LUNEOS_DEVICE_CODENAME in /run/luneos-device/device.env (luneos-device-config;
# lower-cased if no profile has the name as it is), so one generic image
# carries all of them. A build for one machine can also install its own
# profile as 10-<machine>.conf; loading both is harmless.
file(GLOB KBDSCROLL_PROFILES ${CMAKE_CURRENT_SOURCE_DIR}/profiles/*.conf)
install(FILES ${KBDSCROLL_PROFILES}
        DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}/kbdscroll.conf.d/by-codename)

set(SYSTEMD_UNITDIR "${CMAKE_INSTALL_PREFIX}/lib/systemd/system"
    CACHE PATH "Where kbdscroll.service is installed")
install(FILES kbdscroll.service DESTINATION ${SYSTEMD_UNITDIR})

# Host tests: the daemon's protocol handling, driven directly. Not installed.
# tests/run-host-tests.sh runs the same file with gcc and clang, plain and
# under ASan and UBSan.
option(BUILD_TESTING "Build the host tests" ON)
if(BUILD_TESTING)
    enable_testing()
    add_executable(test_kbdscroll tests/test_kbdscroll.c)
    add_test(NAME kbdscroll COMMAND test_kbdscroll)
endif()
