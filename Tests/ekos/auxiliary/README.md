# Ekos Auxiliary Unit Tests

This document describes the unit tests in `Tests/ekos/auxiliary/`.  Currently
all tests are in the `darkprocessor/` subdirectory, which tests the dark frame
processing and defect map subsystem in `kstars/ekos/auxiliary/`.

---

## Subdirectories

| Subdirectory | What it tests |
|---|---|
| [`darkprocessor/`](darkprocessor/) | Dark frame defect detection and hot-pixel correction |

---

## Prerequisites

- `INDI_FOUND` + `CFITSIO_FOUND` (parent CMakeLists guards)
- The test loads a FITS file from a relative path; run from the build root.

---

## Running the tests

```bash
# From the repo build directory:
./build/Tests/ekos/auxiliary/darkprocessor/testdefects -v2
./build/Tests/ekos/auxiliary/darkprocessor/testsubtraction -v2
```

---

## Test inventory

### `darkprocessor/testdefects.cpp` — Hot pixel detection and correction

Tests `DefectMap` and `DarkProcessor::normalizeDefects()`.

**Scenario:**

1. Loads `hotpixels.fits` — a synthetic dark frame with known hot pixels.
2. Injects two additional artificial hot pixels at known positions (50,10) and
   (80,80) by writing `255` directly into the image buffer.
3. Scans the buffer for pixels above threshold 200; verifies that exactly
   5 hot pixels are found (3 original + 2 injected).
4. Calls `DefectMap::setDarkData()` to build the defect map from the dark frame.
5. Calls `DarkProcessor::normalizeDefects()` to correct the image.
6. Re-scans all previously-hot pixel positions and verifies that every one is
   now below 100 — confirming that the correction replaced hot pixels with
   interpolated values from their neighbours.

**Note:** The test is skipped (via `QSKIP`) if `hotpixels.fits` cannot be
found in the expected relative path, so it passes harmlessly in environments
where the FITS file is absent.

---

### `darkprocessor/testsubtraction.cpp` — Dark frame subtraction

Tests `DarkProcessor`'s master dark subtraction path — applying a scaled dark
master to a light frame and verifying that the result matches the expected
pixel values within tolerance.

---

## Known gaps in `kstars/ekos/auxiliary/`

Most Ekos auxiliary classes have **no unit tests**.  The following are the most
valuable targets:

| Class | Source file | What to test |
|---|---|---|
| `FilterManager` | `filtermanager.cpp` | Filter sequence, offset application, autofocus triggers |
| `SolverUtils` | `solverutils.cpp` | Plate-solve result parsing and WCS extraction |
| `RotatorUtils` | `rotatorutils.cpp` | Position angle ↔ mechanical angle conversion |
| `OpticalTrainManager` | `opticaltrainmanager.cpp` | CRUD operations on the SQLite optical train store |
| `DarkLibrary` | `darklibrary.cpp` | Dark frame matching by temperature, exposure, and binning |
| `FilterOffsets` | `buildfilteroffsets.cpp` | Focus offset calculation from filter measurements |
