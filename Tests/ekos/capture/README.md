# Capture Unit Tests

This document describes the unit tests in `Tests/capture/`.  These tests cover
the capture subsystem classes in `kstars/ekos/capture/` — specifically the
placeholder path generator and the sequence-job state machine.  No running
KStars window, no camera hardware, and no INDI connection are required.

---

## Prerequisites

- `INDI_FOUND` (parent CMakeLists guard)
- `test_placeholderpath` is not built on Windows (`if (NOT WIN32)`)

---

## Test fixtures

CSV data files used by `test_placeholderpath` as truth tables.  They are copied
to the build directory at build time.

| File | Purpose |
|---|---|
| `testSchedulerProcessJobInfo_data.csv` | Scheduler-driven placeholder path generation |
| `testFullNamingSequence_data_small.csv` | Full naming-sequence format |
| `testFlexibleNaming_data_small.csv` | Flexible naming mode |
| `testFlexibleNamingChangeBehavior_data_small.csv` | Behaviour changes when switching naming mode |

Sequence files (`.esq`):

| File | Purpose |
|---|---|
| `2x10x20s_refocus1min.esq` | Capture sequence with periodic re-focus |
| `decimal_values.esq` | Sequence with decimal exposure and dithering settings |
| `jobs_using_refocus.esl` | Scheduler list that exercises re-focus logic |

---

## Running the tests

```bash
./build/Tests/capture/test_placeholderpath -v2
./build/Tests/capture/test_sequencejobstate -v2

# Run a single function:
./build/Tests/capture/test_placeholderpath testFlexibleNaming -v2
```

---

## Test inventory

### `test_placeholderpath.cpp` — FITS file path generation

Tests `PlaceholderPath::generateSequenceFilename()` and the related
`Scheduler::processJobInfo()` helper.  The placeholder path system assembles
the directory and filename for each captured FITS frame from a template that
may include tokens such as `%T` (target), `%F` (filter), `%e` (exposure),
`%s` (sequence number), and others.

Each CSV fixture file provides a set of input parameters (job name, filter,
exposure, count, ...) and the expected output path.  The test iterates all
rows and asserts that the generated path matches the expected value exactly.

Tested naming modes:

- **Full naming sequence** — all tokens expanded; path includes date, time,
  filter, target, and frame counter.
- **Flexible naming** — user-configurable subset of tokens.
- **Flexible naming behaviour change** — verifies that switching naming modes
  at runtime produces paths consistent with the new mode from the next frame.
- **Scheduler-driven** — paths generated when the scheduler calls
  `processJobInfo()` with a loaded `.esl`/`.esq` pair.

---

### `test_sequencejobstate.cpp` — Sequence job state machine

Tests `SequenceJobState`, the state machine that governs a single capture job
as it progresses through preparation steps (filter change, autofocus, guiding
check, temperature settle) before triggering the actual exposure.

Key scenarios:

- State transitions: `IDLE → PREPARING → PROGRESS → COMPLETE`
- Filter change required vs. not required
- Autofocus triggered by filter change vs. HFR threshold vs. timer
- Temperature settle: wait completes vs. times out
- Abort: state machine resets correctly from any state
- `isComplete()` / `isAborted()` / `isPreparing()` helper methods
