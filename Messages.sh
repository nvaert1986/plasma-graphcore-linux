#!/bin/sh
# SPDX-FileCopyrightText: 2026 plasma-graphcore-linux contributors
# SPDX-License-Identifier: GPL-3.0-or-later
# Extracts translatable strings into $podir (KDE convention; XGETTEXT is set by the caller)
$XGETTEXT $(find src/lib src/daemon src/ctl -name '*.cpp' -o -name '*.h') -o $podir/plasma-graphcore-linux.pot
# The System Settings page, under its own catalog (KCMs use their plugin id)
$XGETTEXT src/kcm/*.cpp -o $podir/kcm_graphaccounts.pot
$XGETTEXT --join-existing -L JavaScript src/kcm/ui/*.qml -o $podir/kcm_graphaccounts.pot
