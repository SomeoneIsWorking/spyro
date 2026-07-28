---
id: 26
title: Rebuilding while the gate is running silently invalidates its measurement
status: resolved
symptom: gate reports numbers for a binary that no longer exists — it built one, then something overwrote scratch/bin/spyro_port mid-run
tags: gate,workflow,instrument
created: 2026-07-28
updated: 2026-07-28
---

HAPPENED, not hypothetical. A gate run was in flight when an upstream psxport rebase landed and the port was rebuilt against it. The gate had already built and started its 40s run; the binary underneath it was replaced. Its output would have described a mixture of two builds, and nothing in the output would have said so. The run was killed by PID and re-run rather than quoted.

WHY THIS IS THE SAME CLASS AS THE OLD BUG. This gate already spent its whole life reporting PASS on a segfaulting port because 'timeout -s KILL' swallows the exit status — a gate measuring something other than what the reader thinks it measured. 'Built binary X, reported on binary Y' is that failure again, from the other end.

WHY IT IS EASY TO HIT HERE. The gate takes minutes (build + 40s run + checks) and is naturally started in the background, so any parallel work that touches the build invalidates it. Nothing currently prevents or detects that.

CANDIDATE FIX. After the build, record the binary's identity (size+mtime, or a hash); re-check it immediately after the run and REFUSE to report if it changed, exiting non-zero with 'the binary was replaced mid-run'. A lock file would also work but is worse: it would block legitimate parallel work rather than just refusing to draw a false conclusion from it.

RELATED HAZARD, worth stating in the same place: do not EDIT tools/gate.sh while it is running. bash reads a script incrementally, so an in-place edit can make a running gate execute spliced garbage.

### Resolution (2026-07-28)
FIXED and TESTED. tools/gate.sh now pins the binary's identity (size:mtime) immediately after its build and re-checks it after the run; if it changed it prints what it saw and exits 2 instead of reporting numbers.

Chose refuse-to-report over a lock file deliberately: a lock would block legitimate parallel work, whereas this only stops a false conclusion being drawn from it.

Verified it actually fires rather than assuming — touched scratch/bin/spyro_port 10s into a run and the gate refused with 'THE BINARY WAS REPLACED MID-RUN (21646040:1785267209 -> 21646040:1785267458)' and a real exit code of 2. (Note the sizes are identical there: a same-size rebuild is exactly the case a naive size-only check would miss, which is why mtime is in the identity.) Also confirmed the exit code separately from the message, because piping the gate through 'tail' masks its status with tail's — worth knowing for anyone wiring this into CI.

The related hazard stands and is not fixable in code: do not EDIT tools/gate.sh while it is running; bash reads a script incrementally and an in-place edit can make a running gate execute spliced garbage.
