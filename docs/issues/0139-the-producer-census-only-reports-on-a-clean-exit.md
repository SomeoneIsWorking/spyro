# 0139 — the producer census only reports on a clean exit, so every probe-driven run prints nothing

`core.rsub.census.report("spyro run-complete")` at `game/core/runtime_run.cpp:61` is the answer to
"which producer owns the thing I am looking at" — the question issue 0138 needed. It was observed to
print nothing on a rebuilt run. This is the static cause, and it is a defect, not a misreading.

## The call site is reachable only two ways, and both are easy to not take

`reportRuntimeRun` is called at `game/core/main.cpp:101`, **after** the frame loop. The loop's
`shouldEnd()` is true in exactly two cases:

| exit | condition | where |
|---|---|---|
| field cap | `PSXPORT_NATIVE_FRAMES != 0` **and** `fields_ >= fieldLimit_` | `fields_` is incremented only at `titles/spyro1/core/spyro1_field_scheduler.cpp:376` |
| REPL `end` | the REPL returns `-2` | `requestEnd()`, called from exactly one place, `titles/spyro1/core/spyro1_field_scheduler.cpp:276`, inside `serviceRepl()`, which returns immediately unless `PSXPORT_REPL=1` |

With `PSXPORT_NATIVE_FRAMES=0` (the default `game/core/main.cpp:81`) and no REPL `end`, the loop never
exits and **`reportRuntimeRun` is never reached**. No `lucent::info`/`warn` there is filtered, and
`report()` refusing loudly when unfed does not help, because a killed process cannot report at all.

## Which routes take an exit, and which do not

- `tools/drive.py` sends the REPL `end` (`Port.end`, `tools/drive.py:221`) → **reaches it.**
- `tools/shot.py` sends `end` explicitly, with a comment at `tools/shot.py:124` recording that `quit`
  merely detaches and left the capture to be SIGKILLed.
- `tools/demo_run.py` **does not.** It pops `PSXPORT_REPL` (`:60`) because nothing will speak to it,
  sets no cap, and waits with `subprocess.run(..., timeout=…)`; on expiry the process is killed and
  `report(log, "outlived its N s clock — still running when it was killed")` is printed instead.

## The measurement

**1 of 110** logs under `scratch/logs/` contains the census report. It is `scratch/logs/drive.log`
(2026-09-28T05:26:56), 13 rows:

    [producers] spyro run-complete: 13 row(s); prims seen 10620948 = attributed 10620948 + …
      pc-only 0x8004EBA8  native 4679964  frames 2281  terrain:direct
      guest   0x800258F0  native 2776584  frames 2281  world:static
      guest   0x8001F798  native 2517162  frames 2281  actor:opaque
      guest   0x80023AC4  native  473808  frames  868  pairedactor:normal
      guest   0x80022A2C  native  146134  frames  588  spriteq:RasterizeSpritePrimQueue.screen
      … 8 more rows, 0 with any guest-leg prims

`scratch/logs/demo_run.log` (1.1 MB, the attract route) has no census line. So the instrument is
armed, wired, fed, and **mute on exactly the routes that reach gameplay unattended.**

## What the finding was worth

The census row **did** answer issue 0138's attribution question — `world:static` at `0x800258F0` is
the producer that draws the terrain the pool is cut into, and the absence of any water-named row
ruled out a dedicated water producer. That answer is in a `drive.py` log, so it was recoverable, but
only because a 3,052-field gameplay run happened to be on disk.

## The exact observation that would confirm or refute this

`tools/demo_run.py` with a log no run has written to before, then `grep -c "row(s); prims seen"`
against it. Zero confirms the kill path; non-zero refutes this analysis and means some other route
reaches `reportRuntimeRun`. **Not performed — no product slot was free, and running the product was
out of scope for this session.** The static reading above stands on the code, not on a run.

## The fix, when a slot is free

Not a `requestEnd()` call: the census is a run-end report and the run genuinely has no end on those
routes. The options are to report from a bounded cadence during the run, or to have the harness ask
for the report before it kills the process. Either is a framework-shaped decision, not a one-liner
here, and picking it needs the product slot this session did not have.
