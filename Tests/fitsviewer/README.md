# FITS Viewer Unit Tests

This document describes the unit tests in `Tests/fitsviewer/`.  These tests
cover `FITSData` — the core class responsible for loading, processing, and
analysing FITS images in KStars.  No KStars window or INDI connection is
required.

---

## Prerequisites

- `CFITSIO_FOUND` — required for any FITS I/O
- `StellarSolver_FOUND` — required for the star-detection tests within
  `testfitsdata`

If `StellarSolver` is not found the `testfitsdata` target is not built (see
`CMakeLists.txt`).

---

## FITS fixture files

| File | Contents |
|---|---|
| `m47_sim_stars.fits` | Simulated star field used for star-detection and HFR tests |
| `ngc4535-autofocus1.fits` | NGC 4535 frame 1 of 3, used for auto-focus sequence tests |
| `ngc4535-autofocus2.fits` | NGC 4535 frame 2 of 3 |
| `ngc4535-autofocus3.fits` | NGC 4535 frame 3 of 3 |
| `bahtinov-focus.fits` | Bahtinov mask diffraction-spike image used to test Bahtinov focus analysis |

These files are copied to the build directory by `ADD_CUSTOM_COMMAND` in
`CMakeLists.txt`.  The `ngc4535-autofocus1.fits` file is also used by the
polar-align tests (see `Tests/polaralign/`).

---

## Running the tests

```bash
./build/Tests/fitsviewer/testfitsdata -v2

# Run a specific test function:
./build/Tests/fitsviewer/testfitsdata testStarDetection -v2
```

---

## Test inventory

### `testfitsdata.cpp`

#### FITS loading

Verifies that `FITSData::loadFromFile()` correctly reads FITS files,
populates the image buffer, and reports the correct width, height, bit depth,
and pixel statistics.

#### Star detection

Using `m47_sim_stars.fits`, verifies that `FITSData::findStars()` (via
StellarSolver) detects the expected number of stars and that individual star
positions and HFR values are within tolerance.

#### Auto-focus sequence

Uses the three `ngc4535-autofocus*.fits` frames to verify that HFR measurements
trend correctly as the focuser moves — forming the expected V-curve shape that
the focus algorithm needs as input.

#### Bahtinov focus analysis

Loads `bahtinov-focus.fits` and calls the Bahtinov mask analyser.  Verifies
that the three diffraction spikes are detected and that the computed focus offset
(deviation of the central spike from the midpoint of the outer spikes) is within
the expected range for a correctly focused system.

#### Statistics

Tests `FITSData::calculateStats()`: mean, median, standard deviation, minimum,
and maximum pixel values for the loaded images.

#### Image debayering

Tests CFA (Bayer matrix) debayering for FITS files that include a `BAYERPAT`
keyword, verifying that the correct RGGB / BGGR / GRBG / GBRG patterns produce
the expected colour-channel separation.
