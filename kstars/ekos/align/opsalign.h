/*
    SPDX-FileCopyrightText: 2017 Jasem Mutlaq <mutlaqja@ikarustech.com>
    SPDX-FileCopyrightText: 2017 Robert Lancaster <rlancaste@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "ui_opsalign.h"
#include "parameters.h"

#include <QWidget>

class KConfigDialog;

namespace Ekos
{
class Align;

class OpsAlign : public QWidget, public Ui::OpsAlign
{
        Q_OBJECT

    public:
        explicit OpsAlign(Align *parent);
        virtual ~OpsAlign() override = default;

        typedef enum { ROTATOR_ANGLE = 0, POSITION_ANGLE = 1 } FlipPriority;
        void setFlipPolicy(FlipPriority Priority);

    public Q_SLOTS:
        void reloadOptionsProfiles();

    protected:

    private Q_SLOTS:
        void slotApply();

    Q_SIGNALS:
        void settingsUpdated();
        void needToLoadProfile(const QString &profile);

    private:
        QList<SSolver::Parameters> optionsList;
        KConfigDialog *m_ConfigDialog { nullptr };
        Align *alignModule { nullptr };
};
}
