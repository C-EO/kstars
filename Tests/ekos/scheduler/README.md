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

---

## Scheduler State Machines

### Startup State Machine

```
checkStartupState() transitions
├── STARTUP_IDLE
│     ├── Pre-startup queue configured?
│     │     ├── YES → STARTUP_PRE_DEVICES_RUNNING → (queue completes) → STARTUP_PRE_DEVICES
│     │     └── NO  → STARTUP_PRE_DEVICES
│
├── STARTUP_PRE_DEVICES
│     └── checkEkosState() / checkINDIState() run
│           └── INDI_READY → STARTUP_POST_DEVICES
│
├── STARTUP_POST_DEVICES
│     ├── Post-startup queue configured?
│     │     ├── YES → STARTUP_POST_DEVICES_RUNNING → (queue completes) → STARTUP_COMPLETE
│     │     └── NO  → STARTUP_COMPLETE
│
├── STARTUP_POST_DEVICES_RUNNING
│     └── queueExecutionCompleted()
│           ├── setStartupState(STARTUP_COMPLETE)
│           └── If schedulerState == IDLE or PAUSED → execute()
│               If schedulerState == RUNNING → setupNextIteration(RUN_SCHEDULER)
│               (RUNNING case arises after weather recovery — see below)
│
├── STARTUP_COMPLETE → return true (proceed to executeJob)
│
└── STARTUP_ERROR → stop()
```

**State reset rules:**

| Function | Condition | Action |
|----------|-----------|--------|
| `start()` | State is ERROR or intermediate | Reset to `STARTUP_IDLE` |
| `start()` | State is `STARTUP_COMPLETE` | **Preserve** (don't re-run startup) |
| `wakeUpScheduler()` | State is `STARTUP_POST_DEVICES` | Run post-startup queue, then reset to `STARTUP_IDLE` after queue starts (see note) |
| `wakeUpScheduler()` | Any other non-IDLE state | Reset to `STARTUP_IDLE` |
| `stop()` (normal) | State is `STARTUP_COMPLETE` | Reset to `STARTUP_IDLE` |

> **Note — weather recovery in `wakeUpScheduler()`:**
> When returning from a weather soft-shutdown, `queueExecutionCompleted()` sets
> `startupState = STARTUP_POST_DEVICES` as a signal that only the post-startup
> phase (unpark dome/mount) needs to run.  `wakeUpScheduler()` **must** read this
> value *before* resetting states to IDLE.  It captures it in a local bool
> `needsPostStartupRecovery` first, resets `startupState` and `shutdownState` to
> IDLE, and *then* (if `needsPostStartupRecovery == true`) starts the post-startup
> queue.  This is critical: both wakeup code paths (timer-based and event-driven)
> clear `weatherGracePeriodActive` *before* `wakeUpScheduler()` reaches the
> recovery check, so `weatherGracePeriodActive()` cannot be used as the trigger.

---

### Shutdown State Machine

```
checkShutdownState() / completeShutdown() transitions
├── SHUTDOWN_IDLE
│     ├── Pre-shutdown queue configured?
│     │     ├── YES → SHUTDOWN_PRE_QUEUE_RUNNING → (queue completes) → see below
│     │     └── NO  → SHUTDOWN_STOPPING_EKOS  +  setupNextIteration(RUN_SHUTDOWN)
│
├── queueExecutionCompleted() for pre-shutdown (SHUTDOWN_PRE_QUEUE_RUNNING):
│     ├── weatherGracePeriodActive?
│     │     ├── YES → SHUTDOWN_COMPLETE  (soft shutdown: Ekos/INDI kept running)
│     │     │         startupState = STARTUP_POST_DEVICES  (recovery signal)
│     │     │         (preemptive wakeup timer already set by startShutdownDueToWeather)
│     │     └── NO  → SHUTDOWN_STOPPING_EKOS
│     │               setupNextIteration(RUN_SHUTDOWN)  ← ensures SHUTDOWN_STOPPING_EKOS
│     │               is processed even when timer is in RUN_WAKEUP mode
│
├── SHUTDOWN_STOPPING_EKOS → completeShutdown()
│     ├── Guard: if weatherGracePeriodActive → SKIP disconnect/stop Ekos/INDI
│     │         (soft shutdown: devices stay running during grace period)
│     ├── Disconnect INDI → INDI_DISCONNECTING → INDI_IDLE
│     ├── Stop Ekos → EKOS_STOPPING → EKOS_IDLE
│     ├── Post-shutdown queue configured?
│     │     ├── YES → SHUTDOWN_POST_QUEUE_RUNNING → SHUTDOWN_COMPLETE
│     │     └── NO  → SHUTDOWN_COMPLETE
│     └── SHUTDOWN_COMPLETE → stop()
│
└── SHUTDOWN_ERROR → stop()
```

> **No pre-shutdown queue during weather soft-shutdown:**
> If no pre-shutdown queue is configured (or the queue file loads with zero tasks),
> `checkShutdownState()` now detects `weatherGracePeriodActive == true` and
> immediately sets `SHUTDOWN_COMPLETE + STARTUP_POST_DEVICES` — exactly the same
> outcome as `queueExecutionCompleted()` produces when a pre-shutdown queue
> finishes with the grace period active.  Ekos/INDI remain running throughout.
> This ensures the weather recovery path (`wakeUpScheduler()` → post-startup queue)
> works correctly even when no pre-shutdown queue is defined.

**State reset rules:**

| Function | Condition | Reset to SHUTDOWN_IDLE? |
|----------|-----------|-------------------------|
| `wakeUpScheduler()` | Any non-IDLE state | YES (critical for weather recovery) |
| `stop()` | Always | YES |

---

### Weather Alert / Grace Period Path

When a weather alert is detected during active imaging:

```
setWeatherStatus(WEATHER_ALERT)
├── m_WeatherShutdownTimer.start(delay)  →  wait N seconds before acting
│
└── startShutdownDueToWeather()
      ├── stopCurrentJobAction()  →  abort current capture/guiding
      ├── setWeatherGracePeriodActive(true)
      ├── enablePreemptiveShutdown(gracePeriodExpiry)
      ├── setupNextIteration(RUN_WAKEUP, gracePeriodMs)  ← timer for Path A
      ├── checkShutdownState()
      │     └── pre-shutdown queue present?
      │           ├── YES → SHUTDOWN_PRE_QUEUE_RUNNING → queue executor runs
      │           │         → queueExecutionCompleted()
      │           │               └── weatherGracePeriodActive?
      │           │                     ├── YES → SHUTDOWN_COMPLETE
      │           │                     │         startupState = STARTUP_POST_DEVICES
      │           │                     └── NO  → SHUTDOWN_STOPPING_EKOS (full shutdown)
      │           └── NO  → SHUTDOWN_STOPPING_EKOS
      │                     completeShutdown() skips disconnect (grace period guard)
      │
      └── Weather improves?
            ├── Path A — timer expires with WEATHER_OK:
            │     wakeUpScheduler() fires (timer)
            │       └── weatherGracePeriodActive == true
            │             weatherStatus == WEATHER_OK
            │             → setWeatherGracePeriodActive(false)   ← cleared HERE
            │             → falls through (no return)
            │             → needsPostStartupRecovery captured BEFORE reset
            │             → startupState reset to STARTUP_IDLE
            │             → shutdownState reset to SHUTDOWN_IDLE
            │             → needsPostStartupRecovery? run post-startup queue
            │             → resume current job
            │
            ├── Path B — weather improves event-driven before timer:
            │     setWeatherStatus(WEATHER_OK)
            │       ├── setWeatherGracePeriodActive(false)   ← cleared HERE
            │       └── setupNextIteration(RUN_WAKEUP, 10ms)
            │     wakeUpScheduler() fires (10ms later)
            │       └── weatherGracePeriodActive == false  (already cleared)
            │             preemptiveShutdown == true
            │             → skips inner weatherGracePeriodActive block entirely
            │             → needsPostStartupRecovery captured BEFORE reset
            │             → startupState reset to STARTUP_IDLE
            │             → shutdownState reset to SHUTDOWN_IDLE
            │             → needsPostStartupRecovery? run post-startup queue
            │             → resume current job
            │
            └── Path C — grace period expires with weather still bad:
                  wakeUpScheduler() fires (timer)
                    └── weatherGracePeriodActive == true
                          weatherStatus != WEATHER_OK
                          ├── gracePeriod == 0 (indefinite wait):
                          │     → setWeatherGracePeriodActive(false)
                          │     → disablePreemptiveShutdown()
                          │     → setWeatherShutdownMonitoring(true)
                          │     → setupNextIteration(RUN_WAKEUP, 5min safety-net)
                          │     → recovery event-driven via setWeatherStatus(WEATHER_OK)
                          └── gracePeriod > 0 (hard deadline exceeded):
                                → setStartupState(STARTUP_IDLE)
                                → setWeatherGracePeriodActive(false)
                                → disablePreemptiveShutdown()
                                → setWeatherShutdownMonitoring(true)
                                → checkShutdownState()  →  full shutdown
```

**Critical invariant in `wakeUpScheduler()`:**

In both Path A and Path B, `weatherGracePeriodActive()` is **already `false`** by
the time `wakeUpScheduler()` reaches its recovery logic.  The post-startup queue
cannot be triggered by checking `weatherGracePeriodActive()`.  The correct
indicator is `startupState == STARTUP_POST_DEVICES`, which was set by
`queueExecutionCompleted()` when the pre-shutdown queue finished.  The
implementation captures this in a local bool:

```cpp
// Read BEFORE the reset loop clears it.
const bool needsPostStartupRecovery =
    (moduleState()->startupState() == STARTUP_POST_DEVICES ||
     moduleState()->startupState() == STARTUP_POST_DEVICES_RUNNING);

// Reset states to IDLE.
if (moduleState()->startupState() != STARTUP_IDLE)
    moduleState()->setStartupState(STARTUP_IDLE);
if (moduleState()->shutdownState() != SHUTDOWN_IDLE)
    moduleState()->setShutdownState(SHUTDOWN_IDLE);

// NOW check the saved flag.
if (needsPostStartupRecovery && Options::schedulerStartupEnabled() && ...)
{
    moduleState()->setStartupState(STARTUP_POST_DEVICES_RUNNING);
    m_queueExecutor->start();
    return; // queueExecutionCompleted() → STARTUP_COMPLETE → execute()
}
execute();
```

**Corresponding integration tests** (in `Tests/kstars_ui/test_ekos_scheduler_ops`):

| Test | What it covers |
|------|----------------|
| `testWeatherSoftShutdownSimulator` | Full simulator path: startup → capture → weather alert → soft shutdown verified (Ekos/INDI still running, mount parks, `STARTUP_POST_DEVICES` set) → WEATHER_OK → post-startup queue runs (mount unparks) → job resumes |
| `testWeatherHardShutdownSimulator` | Full simulator path: startup → capture → weather alert → soft shutdown → grace period expires → hard shutdown verified (Ekos/INDI stop, mount parked, scheduler stops completely) |
| `testWeatherMonitoringModeSimulator` | Full simulator path: startup → capture → weather alert → soft shutdown → indefinite grace period (0) → weather monitoring mode verified (Ekos/INDI stay running, scheduler waits in RUN_WAKEUP) → WEATHER_OK → job resumes |

---

### Scheduler Sleep Paths

```
shouldSchedulerSleep() decision tree
├── Weather alert active AND preemptive shutdown configured:
│     → startShutdownDueToWeather()  (see Weather Alert section above)
│       timerState = RUN_WAKEUP  (grace period timer)
│
├── PreemptiveShutdown=ON  AND  gap > PreemptiveShutdownTime (default 2 h):
│     → pre-shutdown queue runs (if configured)
│     → full Ekos/INDI shutdown
│     → stop() called → timerState = RUN_WAKEUP  (wakeup timer set)
│
├── gap > leadTime (5 min)  AND  can park  (preemptive OFF or gap < threshold):
│     → mount park task queued (via QueueManager) or direct D-Bus park
│     → Ekos/INDI remain running
│     → timerState = RUN_WAKEUP
│
└── gap > leadTime, no parking:
      → scheduler sleeps, nothing parked
      → timerState = RUN_WAKEUP
```

---

### Pre-shutdown Timer Race Fix

**Symptom:** When a pre-shutdown queue was configured and the scheduler entered
preemptive sleep, Ekos/INDI were never stopped — they remained running for the
entire multi-hour sleep window.

**Root cause:** `queueExecutionCompleted()` set `shutdownState = SHUTDOWN_STOPPING_EKOS`
but left `timerState = RUN_WAKEUP`.  `runSchedulerIteration()` only dispatches to
`checkShutdownState()` when the timer is `RUN_SHUTDOWN`, so `SHUTDOWN_STOPPING_EKOS`
was never processed.

**Fix:** `queueExecutionCompleted()` now calls
`setupNextIteration(RUN_SHUTDOWN)` unconditionally after setting
`SHUTDOWN_STOPPING_EKOS`, regardless of the current timer state.

**Test:** `testPreemptiveShutdownTimerSwitchOnQueueComplete` (state injection,
verifies `timerState` switches from `RUN_WAKEUP` → `RUN_SHUTDOWN` and that one
`runSchedulerIteration()` call then reaches `stop()`).
