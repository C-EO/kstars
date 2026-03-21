# KStars Auxiliary Unit Tests

This document describes the unit tests in `Tests/auxiliary/`. These tests cover
core KStars utility classes and do not require a full KStars GUI or Ekos/INDI
setup.

---

## Prerequisites

Most tests in this directory require:
- KStars data initialized (via `KStarsData::Instance()`)
- Some tests may require location data to be available

---

## Running a single test

Qt Test accepts the test function name as a command-line argument:

```bash
./build/Tests/auxiliary/testksalmanac testSchedulerDate2021_06_14 -v2
./build/Tests/auxiliary/testdms -v2
```

All per-test launch configurations are available in `.vscode/launch.json` under
names like `Debug testksalmanac::testSchedulerDate2021_06_14`.

---

## Test inventory

### testksalmanac

Tests for the `KSAlmanac` class which calculates astronomical events like
sunrise, sunset, dawn, dusk, and moon phases.

#### `testDawnDusk`

**Data-driven test** verifying dawn/dusk calculations for various dates:
- Summer solstice (June 21) - short nights
- Winter solstice (December 21) - long nights
- Spring/Autumn equinoxes (March 21, September 21)

**Checks:** Dawn and dusk times are within expected tolerances for each date.

#### `testSchedulerDate2021_06_14`

**Debug test** for the specific date used in `testGreedySchedulerRun`.

The scheduler test `testGreedySchedulerRun` expects dawn at 10:53 UTC (3:53am PDT)
but the scheduler shows 3:23am. This test outputs detailed debug information
to help diagnose the discrepancy:

- Location coordinates
- Midnight local/UTC times
- Dawn/dusk fractions and times
- Sun max/min altitude
- Comparison with expected values

**Purpose:** This is primarily a diagnostic test to understand twilight
calculation differences, not a pass/fail assertion test.

#### `testDawnDuskBounds`

**Data-driven test** checking that dawn/dusk values are within valid bounds
(0.0 to 1.0 as fraction of day) for each month of the year.

Tests the 15th of each month to ensure calculations work year-round.

---

### testdms

Tests for the `dms` (degrees, minutes, seconds) angle class.

---

### testcachingdms

Tests for the `cachingdms` class, a cached version of `dms` for performance.

---

### testcolorscheme

Tests for KStars color scheme handling.

---

### testbinhelper

Tests for binary data helper utilities.

---

### testfov

Tests for Field of View (FOV) calculations.

---

### testgeolocation

Tests for geographic location handling.

---

### testksuserdb

Tests for the KStars user database.

---

### testkspaths

Tests for KStars file path utilities.

---

### testtrixelcache

Tests for the trixel cache used in indexing.

---

### testrectangleoverlap

Tests for rectangle overlap detection algorithms.

---

### testcsvparser

Tests for CSV file parsing.

---

### testfwparser

Tests for fixed-width file parsing.

---

## Debugging Twilight Calculation Issues

The `testksalmanac` test was created to help debug the `testGreedySchedulerRun`
failure where job interruption times don't match expected twilight times.

To debug:

1. Run the test:
   ```bash
   ./build/Tests/auxiliary/testksalmanac testSchedulerDate2021_06_14 -v2
   ```

2. Check the output for:
   - Calculated dawn time (local and UTC)
   - Expected dawn time (10:53 UTC)
   - Difference in minutes

3. Compare with external sources like timeanddate.com for astronomical twilight
   times at the test location (Silicon Valley: lat=37.3894, long=-122.086).

4. If the calculation differs significantly, investigate:
   - `KSAlmanac::findDawnDusk()` algorithm
   - The -18° altitude threshold for astronomical twilight
   - Any horizon constraints affecting the calculation