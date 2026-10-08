---
id: 115
title: One audio sample scans 54,000 disc sectors, so a step stalls three seconds and trips the frame watchdog
status: resolved
symptom: a single CDC_GetCDAudioSample call walks the XA read head from LBA 0 to LBA 53,874 looking for a matching sector, decompressing about 7,400 CHD hunks (122 MB) inside one 22.7 us output sample, so no frame presents for ~3.0 s and the default 3 s watchdog aborts the process
state_items: S011, S020, S022
tags: performance, disc, audio, watchdog, startup, cd-model
created: 2026-09-19
updated: 2026-09-19
---

## What happens

Spyro's sound driver sets XA mode and a subheader filter and then reads, and the port's XA stream
self-fetches sectors until one passes that filter:

```
[xa] setfilter file=1 chan=4
[xa] START streaming @ LBA 0 (mode=C8 filter=1)
[xa] skipping non-matching EOF @ LBA 17 ...
[xa] skipping non-matching EOF @ LBA 53874 ...        <- 3.14 s later
[xa] STOP @ LBA 63565 — 1046950 pull(s) from the SPU, 446 audio sector(s) decoded
```

`mode=C8` is double speed + XA-ADPCM + SF filter. The head starts at **LBA 0** and scans forward.

The scan is unbounded. `xa_decode_next_sector` guards itself to 64 sectors per call, but its caller
does not:

```c
while (s_active && !xs->push_mode && (s_wr - (uint32_t)s_rd) < 2) {
  if (xa_decode_next_sector(xs) == 0 && !s_active) { break; }
}
```

The `break` only fires when the stream died. When the 64-sector guard expires with the stream still
active, the loop simply calls again — so one 1/44100 s output sample can walk the entire disc. It
does: 53,874 sectors, about 122 MB of CHD decompression, in one call.

## What is measured

`PSXPORT_DEBUG=dischunk` (psxport `0380eaf6`) over the `tools/drive.py gameplay` route:

```
disc hunk cache at shutdown: 67589 hunk lookup(s), 59128 hit(s), 8461 fill(s)
(12.5% miss), 3611.9 ms in chd_read, worst fill 21.2 ms
```

| second of the run | fills | ms in chd_read | hunks touched |
|---|---:|---:|---|
| 0 | 405 | 191 | 0..513 |
| 1 | 2152 | 992 | 299..2450 |
| 2 | 2164 | 992 | 2451..4614 |
| 3 | 2827 | 990 | 4615..7441 |
| 4-19 | ~900 | ~440 | 7442..8065 |

Seconds 1-3 are **99% duty inside `chd_read`**, strictly sequential, 8,067 of 8,461 fills through
`disc_read_raw`, exactly 8 sector lookups per hunk. Nothing else is logged in that window: no frame
presents.

## Two hypotheses this refutes

- **Not one slow read.** The worst single `chd_read` is 21.2 ms. The 3 s is thousands of ordinary
  0.43 ms decompressions.
- **Not cache thrash.** 8,066 of the 8,461 fills are first visits to that hunk and the p50 reuse
  distance is 7,597 fills, so widening the one-hunk cache would remove 4.7% of fills at best. The
  cache is doing its job; the access pattern is the defect. No cache change was made.

The earlier reading of this issue — "CD audio pulled through the CHD codec with the hunk cache cold"
— had the right backtrace and the wrong cause. The cold cache is not why; an unbounded scan is.

## The cause is a second drive head

The port has two disc cursors. The data path reads through `cd_override` and tracks the head in
`cd.sec_lba`; the XA stream keeps its own `s_lba`, which starts at 0 and advances only when it
fetches. Real hardware has ONE head: a `ReadS` with no `Setloc` starts wherever the last read left
it, and Spyro interleaves its XA music with level data in the same stream, which is why its driver
can set a filter and read without saying where.

psxport already models this correctly elsewhere. `cdc_native.cpp` runs one drive cursor,
drive-paced, routing XA sectors to the SPU ring (`push_mode`, where "a pull-side self-fetch would
read ahead of the physical head and desync A/V") and everything else to the data FIFO. Spyro does
not use it: `xa_stream_start` is reached only from `cd_override.cpp`.

## What is NOT yet established, and must be before the fix

**No `Setloc` is logged at all before `START streaming @ LBA 0`.** A real drive cannot read from a
position nothing set, so either Spyro positions the head through a path `cd_override`'s `CdControl`
wrapper does not route to `xa_stream_setloc`, or it relies on the head left by a previous data read
that the port serves without moving the XA cursor. Which of those it is decides the fix, and it is a
question about Spyro's sound driver that wants reverse engineering, not a guess.

That is also why the obvious bound is wrong on its own. Pacing the head to the drive rate the guest
asked for (`mode & 0x80` = double speed = 150 sectors/s) makes the scan physical and ends the stall,
but on its own it would mean the head never reaches LBA 53,874 within any real route — it would trade
a 3 s freeze for silence. The pacing and the shared cursor land together or not at all.

## What would resolve it

1. RE Spyro's sound driver far enough to say where it believes the head is when it issues that read.
2. Give the XA stream the drive's cursor rather than its own — by routing Spyro's CD through
   `cdc_native`'s drive model, or by seeding `xa_stream_start` from `cd.sec_lba` when no `Setloc`
   has positioned the head since the last stop.
3. Pace head advance at the guest's declared drive speed, so no future filter miss can race the disc
   inside one audio sample regardless of where the head starts.

Not a longer watchdog: raising `PSXPORT_WATCHDOG` hides it and leaves 3.6 s of wasted decompression
and a head 54,000 sectors from where hardware would have it.

## RESOLVED 2026-09-19 — there was no missing Setloc; CdControl's own Setloc was dropped

The section above asked which of two things was happening, and said the answer decided the fix.
It was neither. **libcd's `CdControl` performs the Setloc itself.**

`CdControl(com, param, result)` at `0x80063EAC` inspects a per-command table at `0x80074DAC` and,
when the command's entry is nonzero, sends command `0x02` carrying `param` BEFORE sending `com`:

```
80063F38  beqz  $s1, 0x80063f68      ; param == NULL -> nothing to send
80063F40  lw    $v0, ($s6)           ; s6 = 0x80074DAC + (cmd << 2)
80063F48  beqz  $v0, 0x80063f68      ; this command takes no position -> skip
80063F4C  addiu $a0, $zero, 2        ; <- Setloc
80063F50  move  $a1, $s1             ; ...with the caller's param
80063F58  jal   0x80064cec
```

The table marks `0x03`, `ReadN (0x06)`, `SeekL (0x15)`, `SeekP (0x16)` and `ReadS (0x1B)` as
position-carrying. It is libcd's table, so it is the same in every PsyQ title.

Spyro's sound driver at `0x800568D0`-`0x800568F0` does exactly what that allows:

```
800568D0  addiu $s0, $sp, 0x18       ; a 3-byte MSF buffer
800568DC  lw    $a0, 0x74b8($at)     ; the track's LBA
800568E0  jal   0x80064094           ; CdIntToPos(lba, &loc)  (+150, /75, /60, BCD)
800568E8  addiu $a0, $zero, 0x1b     ; CdlReadS
800568EC  move  $a1, $s0             ; with the position
800568F0  jal   0x80063eac           ; CdControl
```

So it never issues a Setloc because it does not have to. `cd_override.cpp` replaced `CdControl` and
read only the command byte, dropping the position. The traced parameter `25 14 48` is BCD MSF
25:14:48 = **LBA 113,448**, well inside this 281,270-sector disc — the port started from LBA 0
instead and scanned until it hit unrelated audio, which is why the music was wrong as well as late.

Fixed in psxport `892e9550`. Measured on the same `tools/drive.py gameplay` route:

| | before | after |
|---|---:|---:|
| skipped non-matching sectors | 9 | **0** |
| XA stream start | LBA 0 | **LBA 113,448** |
| disc hunk fills | 8,461 | **841** |
| time in `chd_read` | 3,611.9 ms | **403.2 ms** |
| worst single fill | 21.2 ms | **0.9 ms** |

A `looks_right.py` boot run that previously died with `watchdog STUCK` in `CDC_GetCDAudioSample`
now completes. At 2,600 fields with shots at four fences it captures 4/4 and passes `reaches`,
`widescreen`, `coverage` (1.429 -> 1.912 on all four, each capture a distinct picture by sha256) and
`fps60` (1,253,413 interpolated prims over 1,081 extra presents). The first run of this was quoted at
1,300 fields, where `reaches` passed on 1 capture of 4 requested because the tool did not then check
the count; psxport `10071776` makes that a failure and names the cause — `--frames` bounds FIELDS
while `--shot-at` counts PRESENTATION FENCES, and 1,300 fields produced 868 fences. Oracle parity is
unchanged with widescreen and fps60 both proven live in the product log: 36 checkpoints, 468
decisive comparisons, 0 divergences, `complete: true`.

Item 3 of "What would resolve it" — pacing head advance at the guest's declared drive speed — is NOT
done and is no longer needed to end the stall, since the head now starts where the guest asked. It
remains worth doing on its own merits and is not tracked by this issue.
