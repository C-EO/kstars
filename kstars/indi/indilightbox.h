/*
    SPDX-FileCopyrightText: 2015 Jasem Mutlaq <mutlaqja@ikarustech.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "indidustcap.h"
#include "indistd.h"

namespace ISD
{
/**
 * @class LightBox
 * Handles operation of a remotely controlled light box.
 *
 * @author Jasem Mutlaq
 */
class LightBox : public ConcreteDevice
{
        Q_OBJECT
        Q_CLASSINFO("D-Bus Interface", "org.kde.kstars.INDI.LightBox")
        Q_PROPERTY(uint16_t brightness READ brightness)

    public:
        /**
         * @brief LightStatus enum representing the current state of the light box
         */
        typedef enum
        {
            LIGHT_OFF,
            LIGHT_ON,
            LIGHT_BUSY,
            LIGHT_ERROR,
            LIGHT_UNKNOWN
        } LightStatus;

        explicit LightBox(GenericDevice *parent);

        virtual void processSwitch(INDI::Property prop) override;
        virtual void processNumber(INDI::Property prop) override;

        Q_SCRIPTABLE virtual bool isLightOn();
        LightStatus lightStatus() const
        {
            return m_LightStatus;
        }

        /**
         * @brief brightness Get current brightness level
         * @return Brightness level (0-65535)
         */
        uint16_t brightness() const
        {
            return m_Brightness;
        }

        static const QString getStatusString(LightStatus status, bool translated = true);

    Q_SIGNALS:
        void newLightStatus(ISD::LightBox::LightStatus status);

    public Q_SLOTS:
        /**
         * @brief SetBrightness Set light box brightness levels if dimmable.
         * @param val Value of brightness level.
         * @return True if operation is successful, false otherwise.
         */
        Q_SCRIPTABLE bool setBrightness(uint16_t val);

        /**
         * @brief SetLightEnabled Turn on/off light
         * @param enable true to turn on, false to turn off
         * @return True if operation is successful, false otherwise.
         */
        Q_SCRIPTABLE bool setLightEnabled(bool enable);

    private:
        LightStatus m_LightStatus { LIGHT_OFF };
        uint16_t m_Brightness { 0 };
        static const QList<KLocalizedString> lightStates;
};
}
