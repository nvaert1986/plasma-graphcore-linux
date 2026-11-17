# SPDX-FileCopyrightText: 2026 plasma-graphcore-linux contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# libgraphcore: installed, used by every app of the suite. Symbols keep the default visibility for
# now (SOVERSION 0, no stable ABI promised yet); export macros come before a 1.0
ecm_setup_version(${PROJECT_VERSION}
    VARIABLE_PREFIX GRAPHCORE
    VERSION_HEADER ${CMAKE_CURRENT_BINARY_DIR}/graphcore_version.h
    PACKAGE_VERSION_FILE ${CMAKE_CURRENT_BINARY_DIR}/GraphCoreConfigVersion.cmake
    SOVERSION 0
)

add_library(graphcore SHARED)
add_library(GraphCore::GraphCore ALIAS graphcore)
set_target_properties(graphcore PROPERTIES
    VERSION ${GRAPHCORE_VERSION}
    SOVERSION ${GRAPHCORE_SOVERSION}
    EXPORT_NAME GraphCore
    CXX_VISIBILITY_PRESET default
    VISIBILITY_INLINES_HIDDEN OFF
)

set(graphcore_HEADERS
    lib/accounts.h
    lib/connectivity.h
    lib/graphclient.h
    lib/graphrequest.h
    lib/graphresponse.h
    lib/graphutil.h
    lib/ratelimiter.h
    lib/scopes.h
    lib/serviceclient.h
    lib/signin.h
    lib/throttle.h
    lib/tokenprovider.h
)

target_sources(graphcore PRIVATE
    lib/accounts.cpp
    lib/connectivity.cpp
    lib/graphclient.cpp
    lib/graphrequest.cpp
    lib/graphresponse.cpp
    lib/graphutil.cpp
    lib/ratelimiter.cpp
    lib/serviceclient.cpp
    lib/signin.cpp
    lib/throttle.cpp
    ${graphcore_HEADERS}
)

ecm_qt_declare_logging_category(graphcore
    HEADER graphcore_debug.h
    IDENTIFIER GRAPHCORE
    CATEGORY_NAME graphcore
    DEFAULT_SEVERITY Info
    DESCRIPTION "Microsoft Graph core (plasma-graphcore-linux)"
    EXPORT GRAPHCORE
)

target_include_directories(graphcore
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/lib>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}/graphcore>
)

target_link_libraries(graphcore
    PUBLIC
        Qt6::Core
        Qt6::Network
        KF6::ConfigCore
    PRIVATE
        Qt6::Concurrent
        Qt6::DBus
)

install(TARGETS graphcore EXPORT GraphCoreTargets ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
install(FILES ${graphcore_HEADERS} ${CMAKE_CURRENT_BINARY_DIR}/graphcore_version.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/graphcore COMPONENT Devel)

set(CMAKECONFIG_INSTALL_DIR "${KDE_INSTALL_CMAKEPACKAGEDIR}/GraphCore")
configure_package_config_file(${CMAKE_CURRENT_SOURCE_DIR}/GraphCoreConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/GraphCoreConfig.cmake
    INSTALL_DESTINATION ${CMAKECONFIG_INSTALL_DIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/GraphCoreConfig.cmake ${CMAKE_CURRENT_BINARY_DIR}/GraphCoreConfigVersion.cmake
    DESTINATION ${CMAKECONFIG_INSTALL_DIR} COMPONENT Devel)
install(EXPORT GraphCoreTargets DESTINATION ${CMAKECONFIG_INSTALL_DIR} FILE GraphCoreTargets.cmake NAMESPACE GraphCore::)

# The sign-in and refresh-token code: only the account service (step 3) holds refresh tokens, so
# it is not installed. Until then the tests use it directly
add_library(graphcore_auth STATIC auth/auth.cpp auth/auth.h)
set_target_properties(graphcore_auth PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(graphcore_auth PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/auth)
target_link_libraries(graphcore_auth
    PUBLIC
        graphcore
        Qt6::NetworkAuth
    PRIVATE
        Qt6Keychain::Qt6Keychain
)

# graphaccountsd: the account service. graphaccountsd_test is the same with test hooks (a loopback
# login server and refresh tokens from the environment), built only for the tests, never installed
set(graphaccountsd_SOURCES daemon/accountserver.cpp daemon/accountserver.h daemon/callercheck.cpp daemon/callercheck.h)
ecm_qt_declare_logging_category(graphaccountsd_SOURCES
    HEADER graphaccountsd_debug.h
    IDENTIFIER GRAPHACCOUNTS
    CATEGORY_NAME graphaccountsd
    DEFAULT_SEVERITY Info
    DESCRIPTION "Account service (plasma-graphcore-linux)"
    EXPORT GRAPHCORE
)
add_library(graphaccountsd_core STATIC ${graphaccountsd_SOURCES})
target_include_directories(graphaccountsd_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/daemon ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(graphaccountsd_core PRIVATE GRAPHCORE_BINDIR="${KDE_INSTALL_FULL_BINDIR}")
target_link_libraries(graphaccountsd_core PUBLIC graphcore_auth Qt6::DBus Qt6::Concurrent KF6::ConfigCore)

add_executable(graphaccountsd daemon/main.cpp)
target_link_libraries(graphaccountsd PRIVATE graphaccountsd_core)
install(TARGETS graphaccountsd ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

if(BUILD_TESTING)
    add_library(graphaccountsd_test_core STATIC ${graphaccountsd_SOURCES})
    target_include_directories(graphaccountsd_test_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/daemon ${CMAKE_CURRENT_BINARY_DIR})
    target_compile_definitions(graphaccountsd_test_core PRIVATE GRAPHCORE_BINDIR="${KDE_INSTALL_FULL_BINDIR}" GRAPHACCOUNTS_TEST_HOOKS)
    target_link_libraries(graphaccountsd_test_core PUBLIC graphcore_auth Qt6::DBus Qt6::Concurrent KF6::ConfigCore)
    add_executable(graphaccountsd_test daemon/main.cpp)
    target_compile_definitions(graphaccountsd_test PRIVATE GRAPHACCOUNTS_TEST_HOOKS)
    target_link_libraries(graphaccountsd_test PRIVATE graphaccountsd_test_core)
endif()

configure_file(${CMAKE_SOURCE_DIR}/data/io.github.nvaert1986.GraphAccounts.service.in ${CMAKE_CURRENT_BINARY_DIR}/io.github.nvaert1986.GraphAccounts.service @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/io.github.nvaert1986.GraphAccounts.service DESTINATION ${KDE_INSTALL_DBUSSERVICEDIR})

add_executable(graphaccountsctl ctl/main.cpp)
target_link_libraries(graphaccountsctl PRIVATE graphcore)
install(TARGETS graphaccountsctl ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
