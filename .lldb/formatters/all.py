"""
Main entry point for LLDB formatters

SPDX-FileCopyrightText: 2016 Aetf <aetf@unlimitedcodeworks.xyz>
SPDX-FileCopyrightText: 2026 KStars Team
SPDX-License-Identifier: GPL-2.0-or-later
"""

import os


def __lldb_init_module(debugger, unused):
    """Load all formatter modules"""
    parent = os.path.dirname(os.path.abspath(__file__))
    debugger.HandleCommand('command script import ' + os.path.join(parent, 'qt6.py'))
    debugger.HandleCommand('command script import ' + os.path.join(parent, 'kde.py'))
    debugger.HandleCommand('command script import ' + os.path.join(parent, 'kstars.py'))
