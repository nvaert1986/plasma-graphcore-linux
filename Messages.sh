#!/bin/sh
# SPDX-FileCopyrightText: 2026 plasma-graphcore-linux contributors
# SPDX-License-Identifier: GPL-3.0-or-later
# Extracts translatable strings into $podir (KDE convention; XGETTEXT is set by the caller)
$XGETTEXT $(find src/lib src/daemon src/ctl -name '*.cpp' -o -name '*.h') -o $podir/plasma-graphcore-linux.pot
