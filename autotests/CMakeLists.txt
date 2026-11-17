# SPDX-FileCopyrightText: 2026 plasma-graphcore-linux contributors
# SPDX-License-Identifier: GPL-3.0-or-later

ecm_add_tests(
    accountstest.cpp
    graphutiltest.cpp
    graphclienttest.cpp
    signintest.cpp
    LINK_LIBRARIES GraphCore::GraphCore Qt6::Test
)

ecm_add_tests(
    authtest.cpp
    LINK_LIBRARIES graphcore_auth Qt6::Test
)

# Both use QStandardPaths' test mode (the same config file)
set_tests_properties(accountstest authtest PROPERTIES RESOURCE_LOCK qttest-paths)

ecm_add_tests(
    callerchecktest.cpp
    LINK_LIBRARIES graphaccountsd_core Qt6::Test
)

# The account service end to end, on a private session bus without any on-demand services (no
# wallet, no secret service): the user's own bus and keychain are never touched
find_program(DBUS_RUN_SESSION dbus-run-session)
if(DBUS_RUN_SESSION)
    add_executable(servicetest servicetest.cpp)
    target_link_libraries(servicetest PRIVATE GraphCore::GraphCore Qt6::DBus Qt6::Concurrent Qt6::Network Qt6::Test)
    target_compile_definitions(servicetest PRIVATE GRAPHACCOUNTSD_TEST="$<TARGET_FILE:graphaccountsd_test>")
    add_dependencies(servicetest graphaccountsd_test)
    add_test(NAME servicetest
        COMMAND ${DBUS_RUN_SESSION} --config-file=${CMAKE_CURRENT_SOURCE_DIR}/test-session.conf -- $<TARGET_FILE:servicetest>)
    set_tests_properties(servicetest PROPERTIES ENVIRONMENT "GRAPHACCOUNTS_PRIVATE_BUS=1;QT_QPA_PLATFORM=offscreen" TIMEOUT 120)
else()
    message(WARNING "dbus-run-session not found: the account service test is left out")
endif()
