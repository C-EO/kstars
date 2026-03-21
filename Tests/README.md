# KStars Test Suite

This document is the entry point for the KStars test infrastructure.  It
describes the directory layout, how to build and run the tests, what external
libraries each group of tests needs, and where the known coverage gaps are.

---

## Table of Contents

1. [Test taxonomy](#1-test-taxonomy)
2. [Build prerequisites](#2-build-prerequisites)
3. [Building the tests](#3-building-the-tests)
4. [Running the tests](#4-running-the-tests)
5. [Environment variables](#5-environment-variables)
6. [Directory map and coverage](#6-directory-map-and-coverage)
7. [Known coverage gaps](#7-known-coverage-gaps)

---

## 1. Test taxonomy

| Type | Description | Directories |
|------|-------------|-------------|
| **Unit** | Test a single class or algorithm in isolation; no KStars window, no INDI connection | `auxiliary`, `tools`, `skyobjects`, `fitsviewer`, `focus`, `internalguide`, `polaralign`, `scheduler`, `capture`, `mount`, `datahandlers`, `ekos/auxiliary` |
| **Integration / UI** | Launch the full KStars application and interact with it via Qt signals or D-Bus mock modules; requires a display (or Xvfb) | `kstars_ui` |
| **Lite UI** | Same idea but for the KStars Lite (QML) variant; Linux-only | `kstars_lite_ui` |

CTest labels:

| Label | Meaning |
|-------|---------|
| `stable` | Passes reliably in CI without external dependencies |
| `unstable` | Known to be flaky; excluded from the default CI run |
| `ui` | Requires a running display and a D-Bus session bus |
| `no-xvfb` | Must run on a real X display (not Xvfb) |
| `astrometry` | Requires astrometry.net index files |

---

## 2. Build prerequisites

The top-level `Tests/CMakeLists.txt` enables subdirectories conditionally:

| Subdirectory | Required CMake condition |
|---|---|
| `auxiliary`, `tools`, `skyobjects` | Always built |
| `fitsviewer` | `CFITSIO_FOUND` |
| `scheduler`, `focus`, `polaralign`, `ekos`, `capture` | `INDI_FOUND` + `CFITSIO_FOUND` |
| `internalguide` | `INDI_FOUND` + `CFITSIO_FOUND` + not Windows |
| `kstars_ui` | `UNIX && NOT APPLE && CFITSIO_FOUND` |
| `kstars_lite_ui` | `UNIX && NOT APPLE && CFITSIO_FOUND && BUILD_KSTARS_LITE` |
| `datahandlers` | Always built |
| `skyobjects/test_skypoint` | `NOVA_FOUND` |
| `fitsviewer/testfitsdata` | `StellarSolver_FOUND` (within CFITSIO block) |

Minimum packages for a useful test build on a typical Linux system:
```
libindi-dev    libcfitsio-dev    libnova-dev    stellarsolver
```

---

## 3. Building the tests

```bash
# Configure (from the repo root):
cmake -B build -DBUILD_TESTING=ON

# Build all tests:
cmake --build build --target all -j$(nproc)
```

Test binaries are placed under `build/Tests/<subdirectory>/`.

---

## 4. Running the tests

### Run the full suite
```bash
ctest --test-dir build/Tests --output-on-failure
```

### Run only stable tests (suitable for CI)
```bash
ctest --test-dir build/Tests -L stable --output-on-failure
```

### Run only unit tests (exclude UI tests)
```bash
ctest --test-dir build/Tests -LE ui --output-on-failure
```

### Run a specific test binary directly (recommended for development)
```bash
# Run all tests in the binary with verbose output:
./build/Tests/focus/testfocus -v2

# Run a single test function:
./build/Tests/auxiliary/testksalmanac testDawnDusk -v2

# Run a single function in the scheduler ops suite:
./build/Tests/kstars_ui/test_ekos_scheduler_ops testSimpleJob -v2
```

Per-test VS Code launch configurations are available in `.vscode/launch.json`.

---

## 5. Environment variables

These are set automatically by `Tests/testhelpers.h` via the `KTest::setupTestEnvironment()` function that runs before `main()`.  You can also set them manually to influence test behaviour.

| Variable | Purpose |
|---|---|
| `KSTARS_TEST_SOURCEDIR` | Path to the repository root; used to locate `kstars/data/` at runtime.  Set by CMake via `add_compile_definitions`. |
| `KSTARS_TEST_DATADIR` | Override the KStars data directory independently of the source tree. |
| `KSTARS_TEST_NO_NETWORK=1` | Block all outbound network connections (loopback is always allowed).  Useful in sandboxed environments. |
| `KSTARS_TEST_ENABLE_NETWORK=1` | Explicitly allow outbound connections even when `KSTARS_TEST_NO_NETWORK` would otherwise be set by CI. |
| `HOME` / `XDG_DATA_HOME` / `XDG_CONFIG_HOME` | Redirected to a per-test-run temp dir (`/tmp/kstars-tests-<pid>/`) so tests never touch the real user profile. |

---

## 6. Directory map and coverage

> Legend: ✅ Covered — ⚠️ Partial — ❌ Not tested — 🔲 Stub (needs tests)

| Directory | Source subsystem | Coverage | README |
|---|---|---|---|
| [`auxiliary/`](auxiliary/README.md) | `kstars/auxiliary/` core utilities | ✅ | ✅ |
| [`tools/`](tools/README.md) | `kstars/tools/` math helpers | ⚠️ | ✅ |
| [`skyobjects/`](skyobjects/README.md) | `kstars/skyobjects/` | ⚠️ | ✅ |
| [`skycomponents/`](skycomponents/README.md) | `kstars/skycomponents/` | 🔲 | ✅ |
| [`time/`](time/README.md) | `kstars/time/` | 🔲 | ✅ |
| [`projections/`](projections/README.md) | `kstars/projections/` | 🔲 | ✅ |
| [`fitsviewer/`](fitsviewer/README.md) | `kstars/fitsviewer/` | ✅ | ✅ |
| [`focus/`](focus/README.md) | `kstars/ekos/focus/` algorithms | ✅ | ✅ |
| [`internalguide/`](internalguide/README.md) | `kstars/ekos/guide/internalguide/` | ✅ | ✅ |
| [`polaralign/`](polaralign/README.md) | `kstars/ekos/align/polaralign*`, `poleaxis*` | ✅ | ✅ |
| [`scheduler/`](scheduler/README.md) | `kstars/ekos/scheduler/` (unit) | ✅ | ✅ |
| [`capture/`](capture/README.md) | `kstars/ekos/capture/` | ⚠️ | ✅ |
| [`mount/`](mount/README.md) | `kstars/ekos/mount/` meridian flip state | ✅ | ✅ |
| [`datahandlers/`](datahandlers/README.md) | `kstars/catalogsdb/` | ✅ | ✅ |
| [`ekos/auxiliary/`](ekos/auxiliary/README.md) | `kstars/ekos/auxiliary/` dark processor | ⚠️ | ✅ |
| [`ekos/observatory/`](ekos/observatory/README.md) | `kstars/ekos/observatory/` | 🔲 | ✅ |
| [`ekos/analyze/`](ekos/analyze/README.md) | `kstars/ekos/analyze/` | 🔲 | ✅ |
| [`kstars_ui/`](kstars_ui/README.md) | Full Ekos UI integration (scheduler, capture, focus, guide, align, mount, meridian flip) | ✅ | ✅ |
| [`kstars_lite_ui/`](kstars_lite_ui/README.md) | KStars Lite UI smoke tests | ⚠️ | ✅ |

---

## 7. Known coverage gaps

The following KStars subsystems have **no automated tests** of any kind.
Contributions are welcome — see the stub `README.md` in each directory marked
🔲 above for guidance on what to add.

### High-priority gaps (testable math / logic, no hardware required)

| Subsystem | Source path | What to test |
|---|---|---|
| Sky projections | `kstars/projections/` | Round-trip pixel↔sky conversions for all 6 projector types |
| `KStarsDateTime` / `SimClock` / `TimeZoneRule` | `kstars/time/` | JD/calendar conversions, clock tick, DST rules |
| Solar system objects | `kstars/skyobjects/` | `KSSun`, `KSMoon`, `KSPlanet` position/phase calculations |
| `KSParser` (binary data reader) | `datahandlers/ksparser.cpp` | Reading star catalogue binary files |
| `RobustStatistics` | `kstars/auxiliary/robuststatistics.cpp` | Median, IQR, Q-estimator — used by focus and guide |
| Sky components | `kstars/skycomponents/` | `ArtificialHorizonComponent`, `StarComponent` loading |
| Observatory model | `kstars/ekos/observatory/` | Dome, weather model state machines |

### Medium-priority gaps (require display or more infrastructure)

| Subsystem | Source path | Notes |
|---|---|---|
| Ekos Analyze | `kstars/ekos/analyze/` | Session log parsing can be tested without a display |
| FilterManager | `kstars/ekos/auxiliary/filtermanager.cpp` | Can be unit-tested with a mock filter device |
| SolverUtils / RotatorUtils | `kstars/ekos/auxiliary/` | Math helpers testable in isolation |
| Optical Train Manager | `kstars/ekos/auxiliary/opticaltrainmanager.cpp` | SQLite-backed; testable with in-memory DB |
| Most of `kstars/tools/` | Conjunctions, eclipses, observing list, star hopper, imaging planner | Astronomical math is testable; UI wiring is not |

### Low-priority / infrastructure-dependent gaps

| Subsystem | Reason |
|---|---|
| HiPS rendering (`kstars/hips/`) | Requires display + network access to tile servers |
| Terrain rendering (`kstars/terrain/`) | Requires display and OpenGL |
| INDI device layer (`kstars/indi/`) | Requires live INDI server (covered indirectly by `kstars_ui` tests) |
| EkosLive (`kstars/ekos/ekoslive/`) | Requires live EkosLive cloud server |
| OAL observing log (`kstars/oal/`) | Simple data-transfer classes; low risk |

---

## Shared test infrastructure

Full documentation: [`shared/README.md`](shared/README.md).

### `Tests/shared/kstars_test_macros.h` — Generic Qt Test macros

Provides portable `KVERIFY_SUB`, `KWRAP_SUB`, `KTRY_VERIFY_WITH_TIMEOUT_SUB`,
`KTRY_GADGET`, `KTRY_CLICK`, `KTRY_SET_CHECKBOX`, `KVERIFY_EMPTY_QUEUE_WITH_TIMEOUT`
and 20+ related macros.  **No INDI, no CFITSIO, no display required.**
Available to every test binary (the include path is set globally in
`Tests/CMakeLists.txt`).

New test files should `#include "kstars_test_macros.h"` instead of copying
macro definitions locally.

### `Tests/testhelpers.h` — Environment isolation

Header-only helpers shared by all test binaries:

| Symbol | Purpose |
|---|---|
| `KTest::setupTestEnvironment()` | Redirects `HOME`, `XDG_*`, `TMPDIR` to an isolated per-run temp tree; configures optional network blocking |
| `KTest::installTestData()` | Symlinks or copies `kstars/data/` into the isolated XDG home so `KStarsData` can initialise |
| `KTest::rootDir()` | Returns `/tmp/kstars-tests-<pid>/` |
| `KTest::copyRecursively()` | Recursive directory copy used by `installTestData()` |
| `KTEST_BEGIN()` | Macro: set app name, enable test mode, clean XDG dirs, install data |
| `KTEST_END()` | Macro: clean up XDG dirs after the test run |

### Ekos integration helpers (in `kstars_ui/`, documented in `shared/README.md`)

| File | What it provides |
|---|---|
| `kstars_ui/mockmodules.h` | D-Bus mock objects for all 5 Ekos modules |
| `kstars_ui/test_ekos_helper.h` | `TestEkosHelper` class + INDI-specific macros |
| `kstars_ui/test_ekos_capture_helper.h` | Capture test helpers |
| `kstars_ui/test_ekos_scheduler_helper.h` | Scheduler test helpers |
