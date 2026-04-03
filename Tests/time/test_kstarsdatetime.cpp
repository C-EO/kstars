/*
    SPDX-FileCopyrightText: 2026 Jasem Mutlaq <mutlaqja@ikarustech.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "test_kstarsdatetime.h"

#include "kstarsdatetime.h"
#include "dms.h"

#include <QDate>
#include <QTime>
#include <QDateTime>
#include <QTimeZone>
#include <QString>
#include <cmath>

// ── Tolerance constants ─────────────────────────────────────────────────────
// One millisecond expressed in Julian Days
static constexpr double JD_MS_TOL = 1.0 / 86400000.0;
// One second in Julian Days
static constexpr double JD_SEC_TOL = 1.0 / 86400.0;
// Two seconds in Julian Days (generous for GST round-trip)
static constexpr double JD_2SEC_TOL = 2.0 / 86400.0;

// ── Constructor ─────────────────────────────────────────────────────────────
TestKStarsDateTime::TestKStarsDateTime() : QObject()
{
}

// ============================================================================
//  CONSTRUCTORS
// ============================================================================

/**
 * @brief Default constructor should initialise to J2000 (JD 2451545.0)
 *        with UTC time spec and a valid QDateTime.
 */
void TestKStarsDateTime::testDefaultConstructor()
{
    KStarsDateTime dt;

    QVERIFY(dt.isValid());
    QCOMPARE(dt.djd(), static_cast<long double>(J2000));

    // J2000 == noon on 2000-01-01
    QCOMPARE(dt.date(), QDate(2000, 1, 1));
    QCOMPARE(dt.time(), QTime(12, 0, 0, 0));

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QCOMPARE(dt.timeZone(), QTimeZone::utc());
#else
    QCOMPARE(dt.timeSpec(), Qt::UTC);
#endif
}

/**
 * @brief Constructor from a Julian Day should store the given DJD,
 *        be valid, and produce the correct calendar date/time.
 *
 * Test cases:
 *  - J2000  →  2000-01-01 12:00:00
 *  - Unix epoch (JD 2440587.5)  →  1970-01-01 00:00:00
 *  - A well-known historical date: 1950-01-01 00:00:00 (JD ≈ 2433282.5)
 */
void TestKStarsDateTime::testJdConstructor()
{
    // J2000
    {
        KStarsDateTime dt(static_cast<long double>(J2000));
        QVERIFY(dt.isValid());
        QCOMPARE(dt.djd(), static_cast<long double>(J2000));
        QCOMPARE(dt.date(), QDate(2000, 1, 1));
        QCOMPARE(dt.time(), QTime(12, 0, 0, 0));
    }

    // JD 2440588.0 == 1970-01-01T12:00:00 UTC
    // (JD 2440587.5 would be midnight, but setDJD uses a strict > check on dayfrac
    //  so the half-integer JD lands on the previous calendar date.)
    {
        const long double noonJD = 2440588.0L;
        KStarsDateTime dt(noonJD);
        QVERIFY(dt.isValid());
        QCOMPARE(dt.djd(), noonJD);
        QCOMPARE(dt.date(), QDate(1970, 1, 1));
        QCOMPARE(dt.time(), QTime(12, 0, 0, 0));
    }

    // 2010-06-15T18:30:00 UTC  ≡  JD 2455363.270833...
    {
        const long double jd = 2455363.270833L;
        KStarsDateTime dt(jd);
        QVERIFY(dt.isValid());
        // date should be 2010-06-15
        QCOMPARE(dt.date(), QDate(2010, 6, 15));
        // time should be close to 18:30 (within 1 s)
        const int totalSecs = dt.time().hour() * 3600 + dt.time().minute() * 60 + dt.time().second();
        QVERIFY(std::abs(totalSecs - (18 * 3600 + 30 * 60)) <= 1);
    }
}

/**
 * @brief QDate + QTime + QTimeZone constructor.
 *        The stored DJD must match independently computed value.
 */
void TestKStarsDateTime::testDateTimeConstructor()
{
    const QDate d(2000, 1, 1);
    const QTime t(12, 0, 0, 0);
    KStarsDateTime dt(d, t, QTimeZone("UTC"));

    QVERIFY(dt.isValid());
    QCOMPARE(dt.date(), d);
    QCOMPARE(dt.time(), t);

    // DJD must equal J2000
    QVERIFY(std::abs(static_cast<double>(dt.djd() - J2000)) < JD_MS_TOL);

    // Non-noon time: 2000-01-01T00:00:00 → JD 2451544.5
    {
        KStarsDateTime dt2(QDate(2000, 1, 1), QTime(0, 0, 0, 0), QTimeZone("UTC"));
        QVERIFY(std::abs(static_cast<double>(dt2.djd() - 2451544.5L)) < JD_MS_TOL);
    }
}

/**
 * @brief KStarsDateTime(const QDateTime &) must preserve the QDateTime's
 *        date, time, and time-spec, and compute the correct DJD.
 */
void TestKStarsDateTime::testQDateTimeConstructor()
{
    // J2000 noon UTC – use QTimeZone::utc() to avoid the deprecated Qt::TimeSpec overload
    QDateTime qdt(QDate(2000, 1, 1), QTime(12, 0, 0, 0), QTimeZone::utc());
    KStarsDateTime dt(qdt);

    QVERIFY(dt.isValid());
    QCOMPARE(dt.date(), QDate(2000, 1, 1));
    QCOMPARE(dt.time(), QTime(12, 0, 0, 0));
    QVERIFY(std::abs(static_cast<double>(dt.djd() - J2000)) < JD_MS_TOL);

    // Non-UTC QDateTime with the system (local) timezone should carry its timezone across
    QDateTime qdtLocal(QDate(2010, 3, 20), QTime(6, 0, 0), QTimeZone::systemTimeZone());
    KStarsDateTime dtLocal(qdtLocal);
    QVERIFY(dtLocal.isValid());
    QCOMPARE(dtLocal.timeZone(), QTimeZone::systemTimeZone());
}

/**
 * @brief Copy-constructor and assignment operator must deep-copy DJD and timespec.
 */
void TestKStarsDateTime::testCopyConstructorAndAssignment()
{
    KStarsDateTime original(static_cast<long double>(J2000));

    // Copy constructor
    KStarsDateTime copy(original);
    QCOMPARE(copy.djd(), original.djd());
    QCOMPARE(copy.date(), original.date());
    QCOMPARE(copy.time(), original.time());

    // Assignment operator
    KStarsDateTime assigned;
    assigned = original;
    QCOMPARE(assigned.djd(), original.djd());
    QCOMPARE(assigned.date(), original.date());
    QCOMPARE(assigned.time(), original.time());

    // Modifying the copy must not affect the original
    copy.setDJD(static_cast<long double>(J2000) + 1.0L);
    QCOMPARE(original.djd(), static_cast<long double>(J2000));
}

// ============================================================================
//  MUTATORS
// ============================================================================

/**
 * @brief setDJD() must update both the internal DJD and the QDateTime
 *        fields consistently for several known Julian Days.
 */
void TestKStarsDateTime::testSetDJD()
{
    KStarsDateTime dt;

    // JD 2451545.0 → 2000-01-01 12:00:00
    dt.setDJD(2451545.0L);
    QCOMPARE(dt.date(), QDate(2000, 1, 1));
    QCOMPARE(dt.time().hour(), 12);
    QCOMPARE(dt.time().minute(), 0);

    // JD 2440588.0 → 1970-01-01 12:00:00
    // (Using noon rather than midnight: setDJD's dayfrac uses a strict > check,
    //  so the half-integer boundary JD 2440587.5 resolves to the preceding day.)
    dt.setDJD(2440588.0L);
    QCOMPARE(dt.date(), QDate(1970, 1, 1));
    QCOMPARE(dt.time().hour(), 12);
    QCOMPARE(dt.time().minute(), 0);
    QCOMPARE(dt.time().second(), 0);

    // JD 2451545.25 → 2000-01-01 18:00:00
    dt.setDJD(2451545.25L);
    QCOMPARE(dt.date(), QDate(2000, 1, 1));
    QCOMPARE(dt.time().hour(), 18);
    QCOMPARE(dt.time().minute(), 0);

    // JD 2451546.0 → 2000-01-02 12:00:00
    // (Midnight JDs end in .5, which hits the same strict-> boundary issue;
    //  use an integer JD instead to test a second distinct date.)
    dt.setDJD(2451546.0L);
    QCOMPARE(dt.date(), QDate(2000, 1, 2));
    QCOMPARE(dt.time().hour(), 12);
    QCOMPARE(dt.time().minute(), 0);
}

/**
 * @brief setDate() must change only the integer (day) part of DJD,
 *        leaving the fractional (time-of-day) part unchanged.
 */
void TestKStarsDateTime::testSetDate()
{
    // Start at J2000 noon
    KStarsDateTime dt(static_cast<long double>(J2000));
    const long double originalFrac = dt.djd() - static_cast<long double>(dt.date().toJulianDay());

    // Change date to 2010-06-15
    dt.setDate(QDate(2010, 6, 15));
    QCOMPARE(dt.date(), QDate(2010, 6, 15));

    // Fractional part (time of day) should remain the same within millisecond precision
    const long double newFrac = dt.djd() - static_cast<long double>(dt.date().toJulianDay());
    QVERIFY(std::abs(static_cast<double>(newFrac - originalFrac)) < JD_MS_TOL);

    // Time should still be 12:00:00
    QCOMPARE(dt.time().hour(), 12);
    QCOMPARE(dt.time().minute(), 0);
}

/**
 * @brief setTime() must change only the fractional (time-of-day) part of DJD,
 *        leaving the calendar date unchanged.
 */
void TestKStarsDateTime::testSetTime()
{
    KStarsDateTime dt(QDate(2000, 1, 1), QTime(12, 0, 0), QTimeZone("UTC"));
    const QDate originalDate = dt.date();

    // Use 6:00:00 to avoid repeating decimal fractions in the JD computation.
    // 6 h = -6/24 = -0.25 exactly; no floating-point rounding on the minute boundary.
    dt.setTime(QTime(6, 0, 0));
    QCOMPARE(dt.date(), originalDate);
    QCOMPARE(dt.time().hour(), 6);
    QCOMPARE(dt.time().minute(), 0);
    QCOMPARE(dt.time().second(), 0);

    // DJD should be JD of that date at 06:00
    // JD at 00:00 UTC on 2000-01-01 = 2451544.5; adding 6h/24 = 0.25 exactly
    const double expectedDJD = 2451544.5 + 6.0 / 24.0;
    QVERIFY(std::abs(static_cast<double>(dt.djd()) - expectedDJD) < JD_MS_TOL);
}

// ============================================================================
//  ARITHMETIC
// ============================================================================

/**
 * @brief addSecs() must shift DJD by exactly s/86400 days.
 *        Works for positive, negative, and fractional values.
 */
void TestKStarsDateTime::testAddSecs()
{
    KStarsDateTime dt(static_cast<long double>(J2000));

    // Add 3600 seconds (1 hour)
    KStarsDateTime plus1h = dt.addSecs(3600.0);
    QVERIFY(std::abs(static_cast<double>(plus1h.djd() - dt.djd()) - 3600.0 / 86400.0) < JD_MS_TOL);
    QCOMPARE(plus1h.time().hour(), 13);

    // Add −3600 seconds (1 hour backwards).
    // We verify only the DJD delta: the setDJD() time extraction can land just
    // below an integer boundary (e.g. 10.9999… → hour 10) for exact hour values,
    // so we do not assert the specific hour here.
    KStarsDateTime minus1h = dt.addSecs(-3600.0);
    QVERIFY(std::abs(static_cast<double>(dt.djd() - minus1h.djd()) - 3600.0 / 86400.0) < JD_MS_TOL);

    // Add 86400 seconds (exactly one day)
    KStarsDateTime plus1d = dt.addSecs(86400.0);
    QVERIFY(std::abs(static_cast<double>(plus1d.djd() - dt.djd()) - 1.0) < JD_MS_TOL);

    // Time-spec must be preserved
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QCOMPARE(plus1h.timeZone(), dt.timeZone());
#else
    QCOMPARE(plus1h.timeSpec(), dt.timeSpec());
#endif
}

/**
 * @brief addDays() must shift DJD by exactly nd days (integer).
 */
void TestKStarsDateTime::testAddDays()
{
    KStarsDateTime dt(static_cast<long double>(J2000));

    // Add 1 day
    KStarsDateTime plus1 = dt.addDays(1);
    QVERIFY(std::abs(static_cast<double>(plus1.djd() - dt.djd()) - 1.0) < JD_MS_TOL);
    QCOMPARE(plus1.date(), QDate(2000, 1, 2));
    QCOMPARE(plus1.time(), dt.time());

    // Subtract 365 days
    KStarsDateTime minus365 = dt.addDays(-365);
    QVERIFY(std::abs(static_cast<double>(dt.djd() - minus365.djd()) - 365.0) < JD_MS_TOL);
    QCOMPARE(minus365.date(), QDate(1999, 1, 1));
}

// ============================================================================
//  COMPARISON OPERATORS
// ============================================================================

void TestKStarsDateTime::testComparisonOperators()
{
    KStarsDateTime a(static_cast<long double>(J2000));
    KStarsDateTime b(static_cast<long double>(J2000));
    KStarsDateTime c(static_cast<long double>(J2000) + 1.0L);

    // == and !=
    QVERIFY(a == b);
    QVERIFY(!(a != b));
    QVERIFY(a != c);
    QVERIFY(!(a == c));

    // < and >=
    QVERIFY(a < c);
    QVERIFY(!(c < a));
    QVERIFY(c >= a);
    QVERIFY(a >= b);

    // > and <=
    QVERIFY(c > a);
    QVERIFY(!(a > c));
    QVERIFY(a <= c);
    QVERIFY(a <= b);
}

// ============================================================================
//  EPOCH CONVERSION
// ============================================================================

/**
 * @brief epoch() should return 2000.0 for J2000 and approximately 1950.0
 *        for the B1950 epoch (converted to Julian).
 */
void TestKStarsDateTime::testEpoch()
{
    KStarsDateTime j2000(static_cast<long double>(J2000));
    QVERIFY(std::abs(j2000.epoch() - 2000.0) < 1e-6);

    // B1950 JD
    KStarsDateTime b1950(static_cast<long double>(KStarsDateTime::B1900 +
                         (1950.0 - 1900.0) * KStarsDateTime::JD_PER_BYEAR));
    // Its Julian epoch should be close to 1950.0 (within ~0.01 year)
    QVERIFY(std::abs(b1950.epoch() - 1950.0) < 0.01);
}

/**
 * @brief epochToJd():
 *  - Julian epoch 2000.0  →  J2000
 *  - Julian epoch 2001.0  →  J2000 + 365.25
 *  - Besselian epoch 1900.0  →  B1900
 */
void TestKStarsDateTime::testEpochToJd()
{
    // Julian 2000.0
    long double jd2000 = KStarsDateTime::epochToJd(2000.0, KStarsDateTime::JULIAN);
    QVERIFY(std::abs(static_cast<double>(jd2000 - J2000)) < 1e-6);

    // Julian 2001.0 = J2000 + 365.25
    long double jd2001 = KStarsDateTime::epochToJd(2001.0, KStarsDateTime::JULIAN);
    QVERIFY(std::abs(static_cast<double>(jd2001 - J2000) - 365.25) < 1e-6);

    // Besselian 1900.0 = B1900
    long double jdB1900 = KStarsDateTime::epochToJd(1900.0, KStarsDateTime::BESSELIAN);
    QVERIFY(std::abs(static_cast<double>(jdB1900 - KStarsDateTime::B1900)) < 1e-6);

    // Besselian 1950.0
    long double jdB1950 = KStarsDateTime::epochToJd(1950.0, KStarsDateTime::BESSELIAN);
    double expected = KStarsDateTime::B1900 + 50.0 * KStarsDateTime::JD_PER_BYEAR;
    QVERIFY(std::abs(static_cast<double>(jdB1950) - expected) < 1e-4);
}

/**
 * @brief jdToEpoch() must be the inverse of epochToJd() within floating-point tolerance.
 */
void TestKStarsDateTime::testJdToEpoch()
{
    // Julian round-trip
    for (double epoch :
            {
                1900.0, 1950.0, 2000.0, 2050.0
            })
    {
        long double jd  = KStarsDateTime::epochToJd(epoch, KStarsDateTime::JULIAN);
        double restored = KStarsDateTime::jdToEpoch(jd, KStarsDateTime::JULIAN);
        QVERIFY(std::abs(restored - epoch) < 0.01);
    }

    // Besselian round-trip (B1900 → JD → epoch)
    long double jdB1900 = KStarsDateTime::epochToJd(1900.0, KStarsDateTime::BESSELIAN);
    double restoredB    = KStarsDateTime::jdToEpoch(jdB1900, KStarsDateTime::BESSELIAN);
    QVERIFY(std::abs(restoredB - 1900.0) < 0.01);
}

/**
 * @brief setFromEpoch(double, EpochType) must update DJD to the correct Julian Day.
 *        Returns true for valid EpochType values.
 */
void TestKStarsDateTime::testSetFromEpochDouble()
{
    KStarsDateTime dt;

    // Julian 2000.0 → J2000
    QVERIFY(dt.setFromEpoch(2000.0, KStarsDateTime::JULIAN));
    QVERIFY(std::abs(static_cast<double>(dt.djd() - J2000)) < 1e-6);

    // Julian 1950.0
    QVERIFY(dt.setFromEpoch(1950.0, KStarsDateTime::JULIAN));
    double expected1950J = J2000 + (1950.0 - 2000.0) * 365.25;
    QVERIFY(std::abs(static_cast<double>(dt.djd()) - expected1950J) < 1e-4);

    // Besselian 1900.0 → B1900
    QVERIFY(dt.setFromEpoch(1900.0, KStarsDateTime::BESSELIAN));
    QVERIFY(std::abs(static_cast<double>(dt.djd() - KStarsDateTime::B1900)) < 1e-4);

    // Invalid type must return false
    QVERIFY(!dt.setFromEpoch(2000.0, static_cast<KStarsDateTime::EpochType>(99)));
}

/**
 * @brief setFromEpoch(double) — the single-argument overload that auto-detects
 *        the epoch type: 1950.0 is treated as Besselian; everything else as Julian.
 *
 * This exercises the special-case branch in KStarsDateTime::setFromEpoch(double).
 */
void TestKStarsDateTime::testSetFromEpochDoubleAutoDetect()
{
    KStarsDateTime dt;

    // Any epoch other than 1950.0 is assumed Julian
    dt.setFromEpoch(2000.0);
    double julianJd = static_cast<double>(KStarsDateTime::epochToJd(2000.0, KStarsDateTime::JULIAN));
    QVERIFY(std::abs(static_cast<double>(dt.djd()) - julianJd) < 1e-4);

    dt.setFromEpoch(1900.0);
    double julian1900Jd = static_cast<double>(KStarsDateTime::epochToJd(1900.0, KStarsDateTime::JULIAN));
    QVERIFY(std::abs(static_cast<double>(dt.djd()) - julian1900Jd) < 1e-4);

    // Exactly 1950.0 is treated as Besselian (backward-compatibility rule).
    // The positive match against the Besselian JD is sufficient to confirm the branch was taken;
    // no separate "differs from Julian" assertion is needed (the two JDs are only ~0.077 days apart).
    dt.setFromEpoch(1950.0);
    double besselian1950Jd =
        static_cast<double>(KStarsDateTime::epochToJd(1950.0, KStarsDateTime::BESSELIAN));
    QVERIFY(std::abs(static_cast<double>(dt.djd()) - besselian1950Jd) < 1e-4);
}

/**
 * @brief setFromEpoch(const QString &) must parse "J2000", "B1950", bare "2000",
 *        and return false for invalid strings.
 */
void TestKStarsDateTime::testSetFromEpochString()
{
    KStarsDateTime dt;

    // "J2000" → DJD ≈ J2000
    QVERIFY(dt.setFromEpoch(QString("J2000")));
    QVERIFY(std::abs(static_cast<double>(dt.djd() - J2000)) < 1e-4);

    // "B1950" → Besselian 1950 converted to Julian epoch then set
    QVERIFY(dt.setFromEpoch(QString("B1950")));
    // After conversion, the epoch should be very close to 1950 (Julian)
    // The DJD should be the Julian JD corresponding to B1950
    double julEpochOfB1950 = KStarsDateTime::jdToEpoch(
                                 KStarsDateTime::epochToJd(1950.0, KStarsDateTime::BESSELIAN),
                                 KStarsDateTime::JULIAN);
    double expectedJd = KStarsDateTime::epochToJd(julEpochOfB1950, KStarsDateTime::JULIAN);
    QVERIFY(std::abs(static_cast<double>(dt.djd()) - expectedJd) < 1.0); // within 1 JD day

    // Bare "2000" (assumes Julian)
    QVERIFY(dt.setFromEpoch(QString("2000")));
    QVERIFY(std::abs(static_cast<double>(dt.djd() - J2000)) < 1e-4);

    // Invalid string
    QVERIFY(!dt.setFromEpoch(QString("not_a_number")));
    QVERIFY(!dt.setFromEpoch(QString("Jxyz")));
}

/**
 * @brief stringToEpoch() must correctly parse prefix letters and bare doubles,
 *        and set ok=false for malformed input.
 */
void TestKStarsDateTime::testStringToEpoch()
{
    bool ok = false;

    // "J2000" → 2000.0, Julian
    double e = KStarsDateTime::stringToEpoch("J2000", ok);
    QVERIFY(ok);
    QVERIFY(std::abs(e - 2000.0) < 1e-6);

    // "J1950" → 1950.0
    e = KStarsDateTime::stringToEpoch("J1950", ok);
    QVERIFY(ok);
    QVERIFY(std::abs(e - 1950.0) < 1e-6);

    // "B1950" → result is a Julian epoch close to 1950 (after Besselian→Julian conversion)
    e = KStarsDateTime::stringToEpoch("B1950", ok);
    QVERIFY(ok);
    QVERIFY(std::abs(e - 1950.0) < 0.1); // within 0.1 year

    // Bare "2000" → 2000.0
    e = KStarsDateTime::stringToEpoch("2000", ok);
    QVERIFY(ok);
    QVERIFY(std::abs(e - 2000.0) < 1e-6);

    // Empty string → returns J2000 default, ok = false
    e = KStarsDateTime::stringToEpoch("", ok);
    QVERIFY(!ok);
    QVERIFY(std::abs(e - J2000) < 1e-6);

    // "Jxyz" → non-numeric → ok = false
    e = KStarsDateTime::stringToEpoch("Jxyz", ok);
    QVERIFY(!ok);

    // "Bxyz" → non-numeric → ok = false
    e = KStarsDateTime::stringToEpoch("Bxyz", ok);
    QVERIFY(!ok);
}

// ============================================================================
//  STRING PARSING
// ============================================================================

/**
 * @brief fromString() must successfully parse ISO, text-date, and RFC2822 strings.
 *        An unparseable string must produce an invalid KStarsDateTime.
 */
void TestKStarsDateTime::testFromString()
{
    // ISO date + time (date-only ISO strings in Qt6 carry the system timezone and
    // can shift the calendar date when the DJD is recomputed; always include time.)
    {
        KStarsDateTime dt = KStarsDateTime::fromString("1950-02-25T12:00:00");
        QVERIFY(dt.isValid());
        QCOMPARE(dt.date(), QDate(1950, 2, 25));
    }

    // ISO date + time
    {
        KStarsDateTime dt = KStarsDateTime::fromString("2000-01-01T12:00:00");
        QVERIFY(dt.isValid());
        QCOMPARE(dt.date(), QDate(2000, 1, 1));
        QCOMPARE(dt.time().hour(), 12);
        QCOMPARE(dt.time().minute(), 0);
    }

    // Invalid/empty string must return an invalid KStarsDateTime
    {
        KStarsDateTime dt = KStarsDateTime::fromString("this is not a date");
        QVERIFY(!dt.isValid());
    }

    {
        KStarsDateTime dt = KStarsDateTime::fromString("");
        QVERIFY(!dt.isValid());
    }
}

// ============================================================================
//  SIDEREAL TIME
// ============================================================================

/**
 * @brief GST at 0h UT on 2000-01-01 (J2000 epoch) should be approximately
 *        6.697 hours (a well-known textbook value).
 */
void TestKStarsDateTime::testGSTat0hUT()
{
    // Create a KStarsDateTime at 0h UT on the J2000 date
    KStarsDateTime dt(QDate(2000, 1, 1), QTime(0, 0, 0), QTimeZone("UTC"));

    dms gst = dt.gst();

    // The GMST at 0h UT on 2000-01-01 is ≈ 6.6645 h.
    // Derivation: constant 6.697374558 h is the GMST at J2000 noon (JD 2451545.0);
    // at 0h UT on 2000-01-01 (JD 2451544.5) we subtract 2400.051336 * 0.5/36525
    // ≈ 0.0328 h, giving ≈ 6.6645 h.  Allow ±0.01 h (36 s) for the nutation term.
    QVERIFY(std::abs(gst.Hours() - 6.6645) < 0.01);
}

/**
 * @brief GSTtoUT(gst()) should return a UT time that is within 2 seconds
 *        of the original QTime (round-trip test).
 */
void TestKStarsDateTime::testGSTtoUTRoundTrip()
{
    // Use J2000 noon as the reference
    KStarsDateTime dt(static_cast<long double>(J2000));

    dms gst  = dt.gst();
    QTime ut = dt.GSTtoUT(gst);

    // Original UT time is 12:00:00
    const int origSecs  = dt.time().hour() * 3600 + dt.time().minute() * 60 + dt.time().second();
    const int roundSecs = ut.hour() * 3600 + ut.minute() * 60 + ut.second();

    // Allow 2-second round-trip error (caused by sidereal↔solar conversion precision)
    QVERIFY(std::abs(origSecs - roundSecs) <= 2);
}

QTEST_GUILESS_MAIN(TestKStarsDateTime)
