/*
    SPDX-FileCopyrightText: 2026 Jasem Mutlaq <mutlaqja@ikarustech.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QtGlobal>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QtTest/QTest>
#else
#include <QTest>
#endif

#include <QObject>

/**
 * @class TestKStarsDateTime
 * @short Unit tests for KStarsDateTime
 *
 * Covers:
 *  - Constructors (default, JD, QDate/QTime, QDateTime, copy)
 *  - setDJD / setDate / setTime round-trips
 *  - addSecs / addDays arithmetic
 *  - Comparison operators
 *  - epoch() / epochToJd() / jdToEpoch() conversions
 *  - setFromEpoch (double, string)
 *  - stringToEpoch parsing
 *  - fromString date-time parsing
 *  - GST computation and GSTtoUT round-trip
 */
class TestKStarsDateTime : public QObject
{
        Q_OBJECT

    public:
        TestKStarsDateTime();
        ~TestKStarsDateTime() override = default;

    private slots:
        // ----- Constructors -----
        void testDefaultConstructor();
        void testJdConstructor();
        void testDateTimeConstructor();
        void testQDateTimeConstructor();
        void testCopyConstructorAndAssignment();

        // ----- Mutators -----
        void testSetDJD();
        void testSetDate();
        void testSetTime();

        // ----- Arithmetic -----
        void testAddSecs();
        void testAddDays();

        // ----- Comparison operators -----
        void testComparisonOperators();

        // ----- Epoch conversion -----
        void testEpoch();
        void testEpochToJd();
        void testJdToEpoch();
        void testSetFromEpochDouble();
        void testSetFromEpochDoubleAutoDetect();
        void testSetFromEpochString();
        void testStringToEpoch();

        // ----- String parsing -----
        void testFromString();

        // ----- Sidereal time -----
        void testGSTat0hUT();
        void testGSTtoUTRoundTrip();
};
