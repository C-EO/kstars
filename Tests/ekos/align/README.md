# Polar Alignment Unit Tests

This document describes the unit tests in `Tests/polaralign/`.  These tests
cover the **polar alignment math** in `kstars/ekos/align/` — specifically
`PolarAlign` and `PoleAxis`.  No KStars window, no plate-solving, and no mount
hardware is required.

---

## Prerequisites

- Always built (no additional CMake guards beyond the CFITSIO+INDI block in the
  parent CMakeLists)
- The test for `test_polaralign` copies `ngc4535-autofocus1.fits` from the
  `fitsviewer/` fixture directory at build time

---

## Running the tests

```bash
./build/Tests/polaralign/test_poleaxis -v2
./build/Tests/polaralign/test_polaralign -v2

# Run a single test function:
./build/Tests/polaralign/test_polaralign testPAA -v2
```

---

## Test inventory

### `test_poleaxis.cpp` — Pole axis geometry

Tests the `PoleAxis` class which computes the mount's actual pole axis direction
from three star observations taken at different hour angles.

Key scenarios:

- **Perfect polar alignment** — all three observations consistent with the
  celestial pole; computed axis matches the true pole exactly.
- **Known misalignment** — a deliberate altitude/azimuth offset is injected;
  the computed axis must match the injected offset within tolerance.
- **Numerical stability** — tests with stars close together in hour angle
  and with targets near the meridian.
- **`calculateRotationMatrix()`** — verifies that the 3D rotation matrix that
  maps the mount axis to the pole is orthogonal and has determinant +1.

---

### `test_polaralign.cpp` — Polar Alignment Assistant

Tests the `PolarAlign` class, the higher-level algorithm that drives the
three-point polar alignment procedure in the Ekos Align module.

Key scenarios:

- **`testPAA`** — loads `ngc4535-autofocus1.fits` as a synthetic star field,
  injects three simulated plate-solve results at different mount positions
  (corresponding to three rotation steps), and verifies that `PolarAlign`
  correctly computes the polar alignment error (altitude and azimuth components)
  and the correction vector.
- **Correction move calculation** — given a computed pole offset, verifies that
  `calculateAdjustment()` returns the correct alt-az knob-turn direction and
  magnitude.
- **Refresh after correction** — simulates re-solving after the user moves the
  alt-az adjusters and checks that the updated error estimate decreases.
