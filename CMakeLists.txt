# SPDX-FileCopyrightText: 2026 plasma-graphcore-linux contributors
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.25)

# The shared Microsoft Graph core of the suite (Graph Mail, Graph Sync): accounts, the Graph client
# and the account service that owns the sign-in
project(plasma-graphcore-linux VERSION 0.14.0 LANGUAGES CXX)

# The floor of the apps that use it
set(QT_MIN_VERSION "6.8.0")
set(KF_MIN_VERSION "6.13.0")
set(KDE_COMPILERSETTINGS_LEVEL "6.0")

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(ECMQtDeclareLoggingCategory)
include(ECMAddTests)
include(ECMSetupVersion)
include(CMakePackageConfigHelpers)
include(FeatureSummary)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core Network NetworkAuth Concurrent DBus Test)
find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS Config)
find_package(Qt6Keychain REQUIRED)

add_compile_definitions(GRAPHCORE_VERSION="${PROJECT_VERSION}")

add_subdirectory(src)

if(BUILD_TESTING)
    add_subdirectory(autotests)
endif()

feature_summary(WHAT ALL INCLUDE_QUIET_PACKAGES FATAL_ON_MISSING_REQUIRED_PACKAGES)
