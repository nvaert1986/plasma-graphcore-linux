# SPDX-FileCopyrightText: 2026 plasma-graphcore-linux contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Microsoft 365 Accounts in System Settings (kcm_graphaccounts): the suite's accounts through the service
kcmutils_add_qml_kcm(kcm_graphaccounts SOURCES kcm_graphaccounts.cpp kcm_graphaccounts.h)
target_link_libraries(kcm_graphaccounts PRIVATE graphcore Qt6::Gui Qt6::Qml KF6::KCMUtilsQuick KF6::CoreAddons KF6::I18n)
# KCMs use their plugin id as translation catalog
target_compile_definitions(kcm_graphaccounts PRIVATE TRANSLATION_DOMAIN="kcm_graphaccounts")
