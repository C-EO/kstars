/*
    Generic Qt Test macro helpers shared across all KStars test directories.

    SPDX-FileCopyrightText: 2021 Wolfgang Reissenberger <sterne-jaeger@openfuture.de>
    SPDX-FileCopyrightText: 2024 KStars Contributors

    SPDX-License-Identifier: GPL-2.0-or-later

    ──────────────────────────────────────────────────────────────────────────
    USAGE
    ──────────────────────────────────────────────────────────────────────────
    Add to your test's CMakeLists.txt:

        include_directories(${kstars_SOURCE_DIR}/Tests/shared)

    Then in your test header or source file:

        #include "kstars_test_macros.h"

    All macros are header-only.  No library link is required.

    ──────────────────────────────────────────────────────────────────────────
    MACRO FAMILIES
    ──────────────────────────────────────────────────────────────────────────

    These macros come in two variants:
      - Plain variant  (e.g. KVERIFY_SUB)    — for use inside subroutines that
                                               return bool.  On failure the
                                               subroutine returns false.
      - WRAP variant   (KWRAP_SUB)            — wraps a statement that may call
                                               QVERIFY internally and returns
                                               false if it triggered a return.

    Plain QtTest macros (QVERIFY, QTRY_*) can only be used in test functions
    that return void.  The *_SUB variants here wrap them to work in bool-
    returning helper functions, which can be composed like:

        bool myHelper() {
            KVERIFY_SUB(someCondition());          // false → return false
            KWRAP_SUB(KTRY_CLICK(module, btn));    // inner QVERIFY → return false
            return true;
        }

    See also: Tests/shared/README.md
*/

#pragma once

#include <QtGlobal>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QtTest/QTest>
#else
#include <QTest>
#endif

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QWindow>
#include <QApplication>

// ─────────────────────────────────────────────────────────────────────────────
// Core subroutine verification helpers
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Subroutine version of QVERIFY.
 *
 * If @p statement is false the enclosing bool function returns false immediately.
 * Use this inside helper functions (return type bool) instead of QVERIFY.
 */
#define KVERIFY_SUB(statement) \
    do { \
        if (!QTest::qVerify(static_cast<bool>(statement), #statement, "", __FILE__, __LINE__)) \
            return false; \
    } while (false)

/**
 * @brief Subroutine version of QVERIFY2 (with description string).
 */
#define KVERIFY2_SUB(statement, description) \
    do { \
        if (statement) { \
            if (!QTest::qVerify(true,  #statement, (description), __FILE__, __LINE__)) \
                return false; \
        } else { \
            if (!QTest::qVerify(false, #statement, (description), __FILE__, __LINE__)) \
                return false; \
        } \
    } while (false)

/**
 * @brief Wrap a statement that internally calls QVERIFY / QTRY_* (void return)
 *        so that it can be used inside a bool-returning helper.
 *
 * Example:
 * @code
 *   bool myHelper() {
 *       KWRAP_SUB(KTRY_CLICK(module, okButton));
 *       return true;
 *   }
 * @endcode
 */
#define KWRAP_SUB(statement) \
    { \
        bool _kwrap_passed = false; \
        [&]() { statement; _kwrap_passed = true; }(); \
        if (!_kwrap_passed) return false; \
    }

// ─────────────────────────────────────────────────────────────────────────────
// Timeout / retry helpers  (subroutine-safe)
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Subroutine version of QTRY_TIMEOUT_DEBUG_IMPL.
 *
 * If @p expr is still false after @p timeoutValue ms, the macro checks whether
 * a longer wait would have succeeded and emits a helpful diagnostic.
 */
#define KTRY_TIMEOUT_DEBUG_IMPL_SUB(expr, timeoutValue, step) \
    if (!(expr)) { \
        const int _kstars_toMs = static_cast<int>(timeoutValue); \
        const int _kstars_stMs = static_cast<int>(step) > 0 ? static_cast<int>(step) : 1; \
        QElapsedTimer _kstars_et; \
        _kstars_et.start(); \
        while (!(expr) && _kstars_et.elapsed() < (2 * _kstars_toMs)) \
            QTest::qWait(_kstars_stMs); \
        if (expr) { \
            QString _msg = QString::fromUtf8( \
                "QTestLib: This check (\"%1\") failed because the timeout (%2 ms) was too short; " \
                "%3 ms would have been sufficient this time.") \
                .arg(QString::fromUtf8(#expr)) \
                .arg(static_cast<qlonglong>(_kstars_toMs)) \
                .arg(static_cast<qlonglong>(_kstars_toMs + _kstars_et.elapsed())); \
            KVERIFY2_SUB(false, qPrintable(_msg)); \
        } \
    }

/**
 * @brief Subroutine version of QTRY_IMPL.
 *
 * Polls @p expr every @p timeoutAsGiven/7 ms (or 50 ms for long timeouts) for
 * up to @p timeoutAsGiven ms.
 */
#define KTRY_IMPL_SUB(expr, timeoutAsGiven) \
    const int _kstars_timeoutMs = static_cast<int>(timeoutAsGiven); \
    const int _kstars_stepMs = (_kstars_timeoutMs < 350) ? (_kstars_timeoutMs / 7 + 1) : 50; \
    { \
        QElapsedTimer _kstars_t; \
        _kstars_t.start(); \
        while (!(expr) && _kstars_t.elapsed() < _kstars_timeoutMs) \
            QTest::qWait(_kstars_stepMs); \
    } \
    KTRY_TIMEOUT_DEBUG_IMPL_SUB((expr), _kstars_timeoutMs, _kstars_stepMs)

/**
 * @brief Subroutine version of QTRY_VERIFY_WITH_TIMEOUT.
 *
 * Polls @p expr for up to @p timeout ms; returns false if it never becomes true.
 */
#define KTRY_VERIFY_WITH_TIMEOUT_SUB(expr, timeout) \
    do { \
        KTRY_IMPL_SUB((expr), timeout); \
        KVERIFY_SUB(expr); \
    } while (false)

// ─────────────────────────────────────────────────────────────────────────────
// Widget-interaction helpers
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Find a child widget of type @p klass named @p name inside @p module.
 *
 * Fails (with QTRY_VERIFY2_WITH_TIMEOUT) if the widget is not found within 10 s.
 * Use this in void test functions.
 */
#define KTRY_GADGET(module, klass, name) \
    klass * const name = (module)->findChild<klass*>(#name); \
    QTRY_VERIFY2_WITH_TIMEOUT(name != nullptr, \
        QString(#klass " '%1' does not exist and cannot be used").arg(#name).toStdString().c_str(), 10000)

/**
 * @brief Subroutine version of KTRY_GADGET.  Returns false if not found.
 */
#define KTRY_GADGET_SUB(module, klass, name) \
    klass * const name = (module)->findChild<klass*>(#name); \
    KVERIFY2_SUB(name != nullptr, \
        QString(#klass " '%1' does not exist and cannot be used").arg(#name).toStdString().c_str())

/**
 * @brief Find a top-level window with title @p name and verify it is visible.
 *
 * Declares a `QWindow* result` in the current scope.
 */
#define KTRY_FIND_WINDOW(result, name) \
    QWindow *result = nullptr; \
    for (QWindow *_w : qApp->topLevelWindows()) { \
        if (_w->title() == (name)) { result = _w; break; } \
    } \
    QVERIFY(result != nullptr); \
    QTRY_VERIFY_WITH_TIMEOUT(result->isVisible(), 1000);

/**
 * @brief Click the QPushButton named @p button inside @p module.
 *
 * Waits up to 5 s for the button to become enabled before clicking.
 */
#define KTRY_CLICK(module, button) \
    do { \
        KTRY_GADGET(module, QPushButton, button); \
        QTRY_VERIFY_WITH_TIMEOUT(button->isEnabled(), 5000); \
        QTest::mouseClick(button, Qt::LeftButton); \
        QTest::qWait(50); \
    } while (false)

/**
 * @brief Subroutine version of KTRY_CLICK.  Returns false on failure.
 */
#define KTRY_CLICK_SUB(module, button) \
    do { \
        KTRY_GADGET_SUB(module, QPushButton, button); \
        KTRY_VERIFY_WITH_TIMEOUT_SUB(button->isEnabled(), 5000); \
        QTest::mouseClick(button, Qt::LeftButton); \
        QTest::qWait(50); \
    } while (false)

// ─────────────────────────────────────────────────────────────────────────────
// Widget value-setter helpers
// ─────────────────────────────────────────────────────────────────────────────

/** @brief Set @p checkbox inside @p module to @p value and verify. */
#define KTRY_SET_CHECKBOX(module, checkbox, value) \
    KTRY_GADGET(module, QCheckBox, checkbox); \
    checkbox->setChecked(value); \
    QVERIFY(checkbox->isChecked() == (value))

/** @brief Subroutine version of KTRY_SET_CHECKBOX. */
#define KTRY_SET_CHECKBOX_SUB(module, checkbox, value) \
    KWRAP_SUB(KTRY_SET_CHECKBOX(module, checkbox, value))

/** @brief Set @p radiobutton inside @p module to @p value and verify. */
#define KTRY_SET_RADIOBUTTON(module, radiobutton, value) \
    KTRY_GADGET(module, QRadioButton, radiobutton); \
    radiobutton->setChecked(value); \
    QVERIFY(radiobutton->isChecked() == (value))

/** @brief Subroutine version of KTRY_SET_RADIOBUTTON. */
#define KTRY_SET_RADIOBUTTON_SUB(module, radiobutton, value) \
    KWRAP_SUB(KTRY_SET_RADIOBUTTON(module, radiobutton, value))

/** @brief Set @p spinbox inside @p module to @p x and verify. */
#define KTRY_SET_SPINBOX(module, spinbox, x) \
    KTRY_GADGET(module, QSpinBox, spinbox); \
    spinbox->setValue(x); \
    QVERIFY(spinbox->value() == (x))

/** @brief Subroutine version of KTRY_SET_SPINBOX. */
#define KTRY_SET_SPINBOX_SUB(module, spinbox, x) \
    KWRAP_SUB(KTRY_SET_SPINBOX(module, spinbox, x))

/** @brief Set @p spinbox (QDoubleSpinBox) inside @p module to @p x and verify (tolerance 0.001). */
#define KTRY_SET_DOUBLESPINBOX(module, spinbox, x) \
    KTRY_GADGET(module, QDoubleSpinBox, spinbox); \
    spinbox->setValue(x); \
    QVERIFY(qAbs(spinbox->value() - (x)) < 0.001)

/** @brief Subroutine version of KTRY_SET_DOUBLESPINBOX. */
#define KTRY_SET_DOUBLESPINBOX_SUB(module, spinbox, x) \
    KWRAP_SUB(KTRY_SET_DOUBLESPINBOX(module, spinbox, x))

/** @brief Set @p combo (QComboBox) inside @p module to text @p value and verify within 1 s. */
#define KTRY_SET_COMBO(module, combo, value) \
    KTRY_GADGET(module, QComboBox, combo); \
    combo->setCurrentText(value); \
    QTRY_VERIFY_WITH_TIMEOUT(combo->currentText() == (value), 1000)

/** @brief Set @p combo (QComboBox) inside @p module to index @p value and verify. */
#define KTRY_SET_COMBO_INDEX(module, combo, value) \
    KTRY_GADGET(module, QComboBox, combo); \
    combo->setCurrentIndex(value); \
    QVERIFY(combo->currentIndex() == (value))

/** @brief Subroutine version of KTRY_SET_COMBO. */
#define KTRY_SET_COMBO_SUB(module, combo, value) \
    KWRAP_SUB( \
        KTRY_GADGET(module, QComboBox, combo); \
        combo->setCurrentText(value); \
        QVERIFY(combo->currentText() == (value)) \
    )

/** @brief Subroutine version of KTRY_SET_COMBO_INDEX. */
#define KTRY_SET_COMBO_INDEX_SUB(module, combo, value) \
    KWRAP_SUB(KTRY_SET_COMBO_INDEX(module, combo, value))

/** @brief Set @p lineedit (QLineEdit) inside @p module to @p value and verify. */
#define KTRY_SET_LINEEDIT(module, lineedit, value) \
    KTRY_GADGET(module, QLineEdit, lineedit); \
    lineedit->setText(value); \
    QVERIFY(lineedit->text() == (value))

/** @brief Subroutine version of KTRY_SET_LINEEDIT. */
#define KTRY_SET_LINEEDIT_SUB(module, lineedit, value) \
    KWRAP_SUB( \
        KTRY_GADGET(module, QLineEdit, lineedit); \
        lineedit->setText(value); \
        QVERIFY(lineedit->text() == (value)) \
    )

// ─────────────────────────────────────────────────────────────────────────────
// State-queue helpers
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Verify that a QQueue is drained within @p delay ms.
 *
 * If items remain after the timeout the test fails (via QFAIL) and prints
 * every remaining item (requires operator<< for the item type).
 */
#define KVERIFY_EMPTY_QUEUE_WITH_TIMEOUT(queue, delay) \
    if (!QTest::qWaitFor([&]() { return (queue).isEmpty(); }, (delay))) { \
        QString _qmsg = QStringLiteral("States not reached: "); \
        QTextStream _qstr(&_qmsg); \
        while (!(queue).isEmpty()) _qstr << (queue).dequeue(); \
        QFAIL(qPrintable(_qmsg)); \
    }

/**
 * @brief Subroutine version of KVERIFY_EMPTY_QUEUE_WITH_TIMEOUT.
 *
 * Emits QWARN and returns false instead of calling QFAIL.
 */
#define KVERIFY_EMPTY_QUEUE_WITH_TIMEOUT_SUB(queue, delay) \
    if (!QTest::qWaitFor([&]() { return (queue).isEmpty(); }, (delay))) { \
        QString _qmsg = QStringLiteral("States not reached: "); \
        QTextStream _qstr(&_qmsg); \
        while (!(queue).isEmpty()) _qstr << (queue).dequeue(); \
        QWARN(qPrintable(_qmsg)); \
        return false; \
    }

// ─────────────────────────────────────────────────────────────────────────────
// Text-field helpers
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Verify (subroutine version) that @p field->text() starts with @p title
 *        within @p timeout ms.
 */
#define KTRY_VERIFY_TEXTFIELD_STARTS_WITH_TIMEOUT_SUB(field, title, timeout) \
    KTRY_VERIFY_WITH_TIMEOUT_SUB( \
        (field)->text().length() >= QString(title).length() && \
        (field)->text().left(QString(title).length()) == QString(title), \
        (timeout))
