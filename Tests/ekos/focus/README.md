# Focus Algorithm Unit Tests

This document describes the unit tests in `Tests/focus/`.  These tests cover
the **focus algorithm implementations** in `kstars/ekos/focus/` — specifically
`FocusAlgorithmInterface` and the concrete implementations created by
`MakeLinearFocuser()`.  No KStars window, no INDI connection, and no camera
hardware is required.

---

## Prerequisites

- `INDI_FOUND` + `CFITSIO_FOUND` (pulled in by the parent CMakeLists guard)
- No KStars data required

---

## Running the tests

```bash
# Run all focus tests:
./build/Tests/focus/testfocus -v2
./build/Tests/focus/testfocusstars -v2

# Run a single test function:
./build/Tests/focus/testfocus L1PHyperbolaTest -v2
```

---

## Test inventory

### `testfocus.cpp` — Linear focuser algorithm

#### `basicTest`

Runs the **Linear** focuser (`FOCUS_LINEAR`, quadratic curve fit) through a
complete simulated V-curve and exercises:

- Correct initial position calculation (respecting `maxTravel` and
  `maxPositionAllowed` limits)
- First pass: position decreases by `initialStepSize` as HFR improves
- Second pass: half step-size, terminates when HFR is within `focusTolerance`
  of the first-pass minimum
- **Retry logic:** when HFR worsens during the second pass the focuser samples
  the same position up to three times before deciding whether to continue or
  finish
- **Miss-tolerance path:** when the focuser misses the desired HFR it resets to
  a polynomial-fit position and retries; tests two and three resets are also
  verified
- **Max iterations exceeded:** after 30 steps the focuser reports failure
  (`solution() == -1`)

#### `restartTest`

Replays a real-world log sequence where the starting position was "too low" on
the V-curve.  Verifies that after several consecutive worsening HFR samples the
focuser correctly resets to a higher initial position rather than continuing
inward.

#### `L1PHyperbolaTest`

Runs the **Linear 1-Pass** focuser (`FOCUS_LINEAR1PASS`) with a hyperbola curve
fit (`FOCUS_HYPERBOLA`) through a synthetic 11-point data set.  Verifies:

- Correct initial position
- All 11 first-pass measurements are consumed without branching
- `solution()` is placed at the curve minimum
- `latestValue()`, `getMeasurements()`, `getPass1Measurements()`, and
  `getTextStatus()` return correct values
- A single additional measurement at the solution confirms `isDone()` and
  `doneReason() == "Solution found."`

#### `L1PParabolaTest`

Same structure as `L1PHyperbolaTest` but uses a parabola curve fit
(`FOCUS_PARABOLA`).  Verifies that `getTextStatus()` reports the correct curve
name.

#### `L1PQuadraticTest`

Same structure but uses a quadratic curve fit (`FOCUS_QUADRATIC`).  Verifies
that L1P with quadratic behaves identically to parabola for this symmetric
data set.

---

### `testfocusstars.cpp` — Focus star statistics

Tests the star HFR / FWHM measurement statistics used to evaluate focus quality
from a list of detected stars, including outlier rejection and weighted averages.
