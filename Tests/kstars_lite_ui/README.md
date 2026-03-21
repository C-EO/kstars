# KStars Lite UI Tests

This document describes the tests in `Tests/kstars_lite_ui/`.  These tests
exercise the **KStars Lite** QML-based UI — the stripped-down variant of
KStars designed for mobile and embedded platforms.

---

## Prerequisites

- Linux only (`UNIX AND NOT APPLE`)
- `BUILD_KSTARS_LITE` must be enabled at configure time
- `CFITSIO_FOUND`
- Links against `KStarsLiteLib` instead of `KStarsLib`
- Requires a display (or Xvfb) at runtime

---

## Building

KStars Lite is not built by default.  Enable it at configure time:

```bash
cmake -B build -DBUILD_KSTARS_LITE=ON -DBUILD_TESTING=ON
cmake --build build -j$(nproc)
```

---

## Running the tests

```bash
./build/Tests/kstars_lite_ui/kstars_lite_ui_tests -v2
```

Or via CTest:

```bash
ctest --test-dir build/Tests -R kstars_lite
```

---

## Test inventory

### `kstars_lite_ui_tests.cpp` / `kstars_lite_ui_tests.h`

Smoke tests for the KStars Lite QML application.  These tests verify that the
Lite application can initialise, render its main QML view, and respond to basic
interactions without crashing.

Specific test functions are defined in `kstars_lite_ui_tests.h`.  Coverage is
currently minimal — this is a known gap.

---

## Known gaps

KStars Lite has very limited test coverage.  The following areas need tests:

- **SkyMapLite rendering** — basic sky map initialisation and coordinate
  transforms for the QML painter.
- **INDI connection** — connecting to a simulated INDI server from the Lite
  client.
- **Settings persistence** — verifying that options saved in the Lite UI are
  written to and read back from the correct QSettings path.
- **Object search** — name resolver and object lookup from the Lite search bar.
