# SPDX-FileCopyrightText: 2026 plasma-graphcore-linux contributors
# SPDX-License-Identifier: GPL-3.0-or-later

ecm_add_tests(
    accountstest.cpp
    graphutiltest.cpp
    graphclienttest.cpp
    LINK_LIBRARIES GraphCore::GraphCore Qt6::Test
)

ecm_add_tests(
    authtest.cpp
    LINK_LIBRARIES graphcore_auth Qt6::Test
)

# Both use QStandardPaths' test mode (the same config file)
set_tests_properties(accountstest authtest PROPERTIES RESOURCE_LOCK qttest-paths)
