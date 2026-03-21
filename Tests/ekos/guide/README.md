# Internal Guide Unit Tests

This document describes the unit tests in `Tests/internalguide/`.  These tests
cover the internal guide algorithm classes in `kstars/ekos/guide/internalguide/`
and the `Calibration` helper.  No KStars window, no PHD2, and no guide camera
hardware is required.

---

## Prerequisites

- `INDI_FOUND` + `CFITSIO_FOUND` (the parent CMakeLists guards this directory)
- Not built on Windows (`if (NOT WIN32)` in CMakeLists)

---

## Running the tests

```bash
./build/Tests/internalguide/testguidestars -v2
./build/Tests/internalguide/teststarcorrespondence -v2
./build/Tests/internalguide/testcalibrationprocess -v2
```

---

## Test inventory

### `testguidestars.cpp` — Multi-star guiding and calibration

#### `basicTest`

Tests the `GuideStars` class end-to-end:

- **`setCalibration()` and arc-second conversion** — verifies that
  `convertToArcseconds()` produces the correct arcseconds-per-pixel value for
  a given focal length, pixel size, and binning.
- **`computeStarDrift()`** — checks the RA/DEC drift calculation at four
  calibration angles (0°, 90°, 180°, 270°) and confirms that the rotation
  matrix correctly maps pixel deltas to RA/DEC arcsecond deltas.
- **`selectGuideStar()`** — verifies that the highest-scoring star is chosen,
  that border proximity disqualifies a candidate, and that a close neighbour
  (distance < threshold) also disqualifies a candidate.
- **`getDrift()`** — with five reference stars set directly, checks that the
  median multi-star drift is computed correctly, that a single outlier star is
  excluded when its drift is > 2 arcsec from the guide star, and that the
  `SkyBackground` SNR threshold is respected.
- **`findMinDistance()`** — verifies the nearest-neighbour distance calculation
  for a three-star list.

#### `calibrationTest`

Tests the `Calibration` class API:

- Pixel ↔ arcsecond conversion with and without binning changes at runtime
  (`setBinningUsed()`)
- RA/DEC pulse rate storage and retrieval
- `rotateToRaDec()` at 0°, 90°, 180°, 270°
- Serialise / restore round-trip (`serialize()` / `restore()`)
- Pier-side flip: restoring on east vs. west pier inverts the rotation angle
  correctly
- `updateRAPulse()` — RA rate correction for declination offset during a session
- `calculate1D()` and `calculate2D()` — single-axis and two-axis calibration
  from measured pixel displacements and pulse durations
- Edge cases: null/NaN declination, unknown pier side

#### `testFindGuideStar`

Development/debugging scaffold for `GuideStars::findGuideStar()` using real
guide FITS files.  The test body is `#if 0`-gated and is skipped in normal
runs.  Activate it locally by removing the `#if 0` guard and providing the
required FITS files.

---

### `teststarcorrespondence.cpp` — Star correspondence across frames

Tests the `StarCorrespondence` class which matches stars between guide frames
when the guide star is lost or the field rotates.  Covers:

- Building a reference star list
- Finding the best match transformation (translation + optional rotation)
- Handling partial star sets (some stars absent in a given frame)
- Robustness to outlier stars

---

### `testcalibrationprocess.cpp` — Calibration process state machine

Tests the `CalibrationProcess` class which drives the RA/DEC pulse-movement
sequence used to calibrate the guider.  Covers the state transitions:
idle → measure RA → measure DEC → compute → done / failed.
