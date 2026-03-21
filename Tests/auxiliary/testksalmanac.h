/*
    SPDX-FileCopyrightText: 2025 KStars Team

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QObject>
#include <QtTest>

class TestKSAlmanac : public QObject
{
        Q_OBJECT

    public:
        TestKSAlmanac(QObject *parent = nullptr);

    private slots:
        void initTestCase();
        void cleanupTestCase();

        /**
         * @brief Test dawn/dusk calculations for various dates
         */
        void testDawnDusk_data();
        void testDawnDusk();

        /**
         * @brief Test the specific date used in scheduler tests (June 14, 2021)
         * This helps debug the testGreedySchedulerRun failure
         */
        void testSchedulerDate2021_06_14();

        /**
         * @brief Test that dawn/dusk values are within reasonable bounds
         */
        void testDawnDuskBounds_data();
        void testDawnDuskBounds();
};