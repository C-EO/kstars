# KStars Scheduler Unit Tests

This document describes the unit tests in `Tests/scheduler/`.  These tests cover
the **planning and scheduling logic** of the Ekos Scheduler in isolation —
no running KStars window, no INDI devices, and no simulated clock.  They focus
on `SchedulerUtils::setupJob()`, sequence-file loading, job time estimation, and
the greedy job-evaluation algorithm.

For the full end-to-end scheduler integration tests (simulated clock, D-Bus mock
modules, full night simulations) see [`Tests/kstars_ui/README.md`](../kstars_ui/README.md).

---

## Prerequisites

- `INDI_FOUND` — the scheduler depends on INDI headers
- `CFITSIO_FOUND` — pulled in by the parent `CMakeLists.txt`
- KStars data **not** required (the test explicitly avoids `KStarsData::Instance()`)

---

## Test fixtures

| File | Purpose |
|---|---|
| `9filters.esq` | A 9-filter capture sequence used as truth data for `loadSequenceQueueTest` and `estimateJobTimeTest` |

Complex `.esl`/`.esq` files in `Tests/scheduler/complex_job/` and other `.esl`
files in this directory are vector files used by the kstars_ui integration
tests, not by `testschedulerunit`.

---

## Running the tests

```bash
# Run all scheduler unit tests:
./build/Tests/scheduler/testschedulerunit -v2

# Run a single test function:
./build/Tests/scheduler/testschedulerunit evaluateJobsTest -v2
```

---

## Test inventory

### `setupGeoAndTimeTest`

Verifies that `SchedulerModuleState::setGeo()` and `setLocalTime()` store and
retrieve `GeoLocation` and `KStarsDateTime` objects correctly.  Also checks
that a freshly created `SchedulerJob` picks up the global local time.

---

### `setupJobTest` (data-driven)

**Data rows:** `ASAP_TO_FINISH`, `START_AT_FINISH_AT`, `ASAP_TO_LOOP`

Exercises `SchedulerUtils::setupJob()` through the private helper
`runSetupJob()`.  For each data row the test:

1. Creates a `SchedulerJob` and calls `setupJob()` with the row's startup
   condition, completion condition, step-pipeline flags (track/focus/align/guide),
   and twilight/horizon enforcement flags.
2. Asserts that every field stored on the job matches what was requested —
   target name, RA/DEC, sequence URL, FITS URL, `minAltitude`, `minMoonSeparation`,
   `maxMoonAltitude`, startup condition + time, completion condition + time/repeats,
   and the four pipeline-step bits.

---

### `loadSequenceQueueTest`

Loads `9filters.esq` via `SchedulerUtils::loadSequenceQueue()` and verifies the
resulting `QList<SequenceJob>` against a hard-coded truth table (`details9Filters`).
Checks filter name, frame count, exposure duration, and frame type for all 9
capture jobs.

**Purpose:** Ensures that the scheduler correctly reads the subset of sequence
fields it needs for time estimation.  A deeper capture-sequence test lives in
`Tests/capture/`.

---

### `estimateJobTimeTest`

Calls `SchedulerUtils::estimateJobTime()` in several configurations and asserts
that `SchedulerJob::getEstimatedTime()` matches the expected value:

| Scenario | Expected result |
|---|---|
| `FINISH_SEQUENCE` (1 repeat) | Sum of (count × exposure) for all 9 filters + step-pipeline overhead |
| `FINISH_REPEAT` (1–10 repeats) | N × exposure sum + overhead |
| `FINISH_LOOP` | Negative (unbounded) |
| `FINISH_AT` with fixed end time | Interval from `now` to end time |
| `START_AT` + `FINISH_AT` | Interval from start time to end time |
| `rememberJobProgress = true` with 2 pre-existing captures | Exposure sum reduced by 2 × 60 s |

---

### `evaluateJobsTest`

Calls `GreedyScheduler::scheduleJobs()` with one- and two-job configurations
and validates the computed schedule:

| Scenario | What is checked |
|---|---|
| Empty job list | Returns empty scheduled list |
| Single job, no constraints | Starts "now"; finishes after exposure sum + overhead |
| Two jobs, first blocked by 80° altitude floor | Job 1 waits until ~11:10 pm; Job 2 starts immediately at 8 pm |
| Two jobs, Job 1 runs past dawn | Job 2 is rescheduled to the **following** evening |

Also verifies that `runsDuringAstronomicalNightTime()` returns `true` for all
scheduled jobs and that the jobs' cached dawn/dusk twilight times match the
global `SchedulerModuleState` values.
