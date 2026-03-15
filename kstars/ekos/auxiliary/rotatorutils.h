/*
    SPDX-FileCopyrightText: 2022 Toni Schriber
    SPDX-License-Identifier: GPL-2.0-or-later
*/


#pragma once

#include "indi/indimount.h"
#include <structuredefinitions.h>

class RotatorUtils : public QObject
{
        Q_OBJECT

    public:
        static RotatorUtils *Instance();
        static void release();

        void initRotatorUtils(const QString &train);
        void setImageFlip(bool state);
        bool checkImageFlip();
        double calcRotatorAngle(double PositionAngle);
        double calcCameraAngle(double RotatorAngle, bool flippedImage);
        double calcOffsetAngle(double RotatorAngle, double PositionAngle);
        void setImagePierside(ISD::Mount::PierSide ImgPierside);
        ISD::Mount::PierSide getMountPierside();
        double DiffPA(double diff);
        void initTimeFrame(const double EndAngle);
        int  calcTimeFrame(const double CurrentAngle);
        void startTimeFrame(const double StartAngle);
        enum CamType
        {
            MAIN_CAM,
            GUIDE_CAM,
            UNDEFINED_CAM
        };
        struct CRRecord
        {
            QString Name;
            CamType Type = UNDEFINED_CAM;
            double Offset;
            FITSImage::Parity Parity;
        };
        bool addRecord(QString CamName, const CamType CameraType,
                       const QString RotatorName);
        bool checkRecord(QString CamName, const CamType CameraType,
                         const QString RotatorName);
        bool updateOffset(const QString CameraName, const double Offset,
                          const FITSImage::Parity Parity);
        QString getRotatorName(const QString CameraName);
        CamType getCamType(const QString CameraName);
        FITSImage::Parity getGuideImageParity();
        double calcGuideCamPA(const double PAMain);
        void clearMap();

    private:
        RotatorUtils();
        ~RotatorUtils();
        static RotatorUtils *m_Instance;

        ISD::Mount::PierSide m_CalPierside {ISD::Mount::PIER_WEST};
        ISD::Mount::PierSide m_ImgPierside {ISD::Mount::PIER_UNKNOWN};
        double m_Offset {0};
        double m_OffsetGuide {0};
        FITSImage::Parity m_ParityGuide;
        bool   m_flippedMount {false};
        ISD::Mount *m_Mount {nullptr};
        double m_StartAngle, m_EndAngle {0};
        double m_ShiftAngle, m_DiffAngle {0};
        QTime  m_StartTime, m_CurrentTime;
        int    m_DeltaTime = 0;
        double m_DeltaAngle = 0;
        int    m_TimeFrame = 0;
        bool   m_initParameter, m_CCW = true;

        QMap<QString, CRRecord> m_CamRotator;

    signals:
        void   changedPierside(ISD::Mount::PierSide index);
};
