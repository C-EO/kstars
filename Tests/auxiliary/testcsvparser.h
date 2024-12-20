/*
    SPDX-FileCopyrightText: 2012 Rishab Arora <ra.rishab@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "ksparser.h"

#include <QtGlobal>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QtTest/QTest>
#else
#include <QTest>
#endif

class TestCSVParser : public QObject
{
    Q_OBJECT
  public:
    TestCSVParser();
    ~TestCSVParser() override = default;

  private slots:
    void initTestCase();
    void cleanupTestCase();
    void CSVMixedInputs();
    void CSVQuotesInQuotes();
    void CSVEmptyRow();
    void CSVNoRow();
    void CSVIgnoreHasNextRow();
    void CSVReadMissingFile();

  private:
    QStringList test_cases_;
    QList<QPair<QString, KSParser::DataTypes>> sequence_;
    QString test_file_name_;
    KSParser *test_parser_ { nullptr };
};
