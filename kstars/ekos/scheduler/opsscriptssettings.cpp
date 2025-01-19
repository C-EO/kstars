/*
    SPDX-FileCopyrightText: 2025 Jasem Mutlaq <mutlaqja@ikarustech.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "opsscriptssettings.h"
#include "kstars.h"
#include <KConfigDialog>

namespace Ekos
{
OpsScriptsSettings::OpsScriptsSettings() : QWidget(KStars::Instance())
{
    setupUi(this);

    //Get a pointer to the KConfigDialog
    m_ConfigDialog = KConfigDialog::exists("schedulersettings");
    if (m_ConfigDialog)
        connect(m_ConfigDialog, &KConfigDialog::settingsChanged, this, &OpsScriptsSettings::settingsUpdated);
}
}
