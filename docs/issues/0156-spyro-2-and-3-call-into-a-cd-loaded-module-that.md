---
id: 156
title: Spyro 2 and 3 call into a CD-loaded module that nothing published as an image; each stock CdRead landing is now an identified resident image
status: resolved
symptom: native-dispatch: guest address 0x80077374 (Spyro 3: 0x80074DEC) resolves to zero or multiple active code images; image identity lookup: claimed by none
tags: spyro2,spyro3,boot,loader,cd,image-identity,lightrec
created: 2026-10-01
updated: 2026-10-01
---

## Answer

**Spyro 2 and 3 called into a loaded code module that nothing had published as an image, so the dispatcher
refused the callee. Publishing each stock-`CdRead` landing as a resident image, identified by the SHA-256
of the bytes now in RAM, moves both boots past the old stop.**

Root cause, from bytes and from the framework's own refusal text. The dispatcher resolves an address to a
single active image (`ImageCatalog::resolve`) and a RAM address that no image covers is the typed
`Fault` (`runtime/cpu/native_dispatch.cpp`, `resolveHostDispatch` step 3). The port's own line said so
before the fix: `guest address 0x80077374 resolves to zero or multiple active code images; image
identity lookup: claimed by none` (Spyro 3: `0x80074DEC`). The framework's stock `CdRead`
(`cd_read_stock_sync`) wrote the bytes and reported the write to the invalidation owner, but it never
told the title that a module had arrived; Spyro 1 publishes its WAD loads because its own
`ArchiveTransfer` does, and Spyro 2/3 have no `ArchiveTransfer` on this route.

### The loader, read out of the executables

`SCUS_944.25` (RAM built the PS-X EXE way, `file[0x800]` to `t_addr` `0x80010000`; decoded with
`external/psxport/tools/disasm.py`):

| fact | bytes |
|---|---|
| loader | `0x80013810(base_lba $a0, dest $a1, length $a2, offset $a3)`: `80013834 addiu $v0,$zero,0x80` (mode), `80013854 jal 0x80058858` (CdCommand 0x0E setmode), `8001387C jal 0x80058C98` (CdIntToPos, `base + offset>>11`), `8001388C jal 0x80058858` (Setloc), `80013894..800138A8` sectors = `(length+0x7FF)>>11`, `800138E0 jal 0x80058108` (CdRead, the address the framework binds) |
| boot call | `80011EDC..80011EF8`: `$a0 = [0x8006D2D8]`, `$a1 = [0x80011110] = 0x8006D264` (the heap base, the first word past bss), `$a2 = [0x800676E4]`, `$a3 = [0x800676E0]`, then `jal 0x80013810`; `80011F0C jal 0x80077374` |
| entry | `80077374 addiu $sp,$sp,-0x40 ; 80077378 sw $s3,0x34($sp)`: a real prologue in the loaded bytes (`$a0 = 1`) |

The header the loader reads first, LBA 500 (1 sector to the same destination, overwritten by the module),
is a table of `(byte offset, byte length)` pairs: `0x800/0x22800, 0x23000/0x12800, 0x35800/0x8000, ...`.
Entry 1 is offset `0x23000` = 70 sectors, so LBA `500 + 70 = 570`, length `0x12800` = **37 sectors**
(Spyro 2) and `0xD800` = **27 sectors** (Spyro 3, same offset). The measured reads agree:
`CdRead 37 sector(s) from LBA 570 -> 0x8006D264` and `CdRead 27 sector(s) from LBA 570 -> 0x800742D0`
(`0x800742D0` is Spyro 3's bss end). The module text is `[0x8006D264, 0x8007FA64)` and
`[0x800742D0, 0x80081AD0)`; their SHA-256 prefixes, computed independently from the disc (`chdman
extractcd`, sector data at raw offset 24), are `a91b532f3f2db952` and `675044820f58e521`, and they are the
exact digests the port logged when it published the images.

Callers of the loader: Spyro 2 `0x80013810` from `0x80011B8C 0x80011EF8 0x80019434 0x80019580 0x8001A990
0x8001D810` and the non-blocking `0x80013918` from five more; Spyro 3 `0x80050578` from seven sites. The
boot loads MANY regions through it, not one module: Spyro 2 issues 10 reads and Spyro 3 13 before the
boot prefix returns, several of them to one address (Spyro 3 loads `0x80081AD0` three times with different
bytes: LBA 676, 932, 966).

### The design

- **Framework (psxport `s23-image`, `752efd9f`).** `cd_read_stock_sync` announces one
  `psx::cd::StockReadLanding` (first LBA, sectors, guest destination, exact bytes) per whole successful
  read, after the last byte is written and reported to the invalidation owner, before the completion is
  queued, through `GameRuntime::stockCdReadLanded` (default: nothing). A read that moved no bytes, failed
  part way, or was refused announces nothing. Test: `test_cd_ready_delivery` (24/24, two new cases).
- **Spyro.** `game/core/image_publication.*` is the ONE digest-and-activate owner; `ArchiveTransfer` (the
  Spyro 1 WAD route) was refactored onto it, so both routes share the SHA-256 identity, the name format
  and the digest refusal. `game/core/stock_read_publication.*` is the Spyro 2/3 landing policy: digest the
  bytes now in RAM, activate a new generation, or request a runtime Fault when the range does not fit main
  RAM or no digest can be produced. `Spyro2Runtime`/`Spyro3Runtime` override `stockCdReadLanded` with one
  line each.
- **Order is the contract.** Publication follows the write because the write's invalidation subtracts its
  bytes from any image already covering them; publishing first would be undone by the load it describes.
  A reload of the same address is a new generation even for identical bytes, and the catalog's
  latest-activation-wins residency makes the earlier module stop resolving, so image-scoped override keys
  cannot outlive a replacement.
- **Test:** `tests/test_stock_read_publication.cpp` drives the shipping `cd_read_stock_sync` with fake
  sectors through both title runtimes, executes the loaded bytes through Lightrec, and covers replacement
  (new generation, old identity gone), an identical reload (same content identity, new generation), a
  zero-sector read, an unpositioned drive and a failing source (nothing published, no fault), and an
  unpublishable landing (Fault, nothing published).

### What moved

`tools/boot_run.py --frames 1500 --fallback-limit 100000000`:

- **Spyro 2:** the old stop at `0x80077374` is gone (0 faults there). With a 64-field boot bound the boot
  then ended by name at resume `0x800772FC`, a `VSync(0)` return inside the module's 12-step fade loop
  (`80077344 slti $v0,$s0,0x10 ; bnez`), not a wedge: the bound was a guess made while the boot stopped at
  the module load. Measured, the boot prefix returns after **43 steps and 133 fields**; Spyro 3 after **336
  steps and 546 fields**. The per-title `kDefaultBootStepFieldLimit` is now 1024 with that measurement
  beside it. Spyro 2 then runs the retail per-frame update/draw for 784 steps / 1,500 fields with
  `faults=0` (see project-state S024 for every counter).
- **Spyro 3: new named stop.** The boot prefix returns, the main loop starts, and the draw `0x8001E638`
  faults at step 363: `ERROR: Segmentation fault in recompiled code: invalid load/store at address PC
  0x1f800400`, guest pc `0x8001C4EC`, `ra=0x1F8003EB`. The instruction at `0x8001C4EC` is the GTE command
  word `0x4A180001` (RTPS; Capstone reports it undecodable), one past `8001C4E8 add $t5,$t5,$t6` with
  `$t6 = 0x1F800000`: a scratchpad-relative pointer that reached `0x1F800400`, the first byte past the
  1 KiB scratchpad. An earlier guest line, `intr timeout(0004:000d)` (libcd's command timeout), was
  printed before it. Cause not established; this is the frontier.

## What is NOT claimed

- No gameplay, and no picture, was observed for either title. Spyro 2's main loop running 784 steps with
  zero faults is not evidence that it draws anything correct.
- Spyro 2 still needs the diagnostic fallback allowance: 37 blocks, all `self_modifying_code`, are the
  shared false positive of issue 0092 section 4.
- Whether every stock read should be published, including pure data reads, is a policy choice borrowed from
  Spyro 1's WAD route (which publishes every WAD read as an image). A data landing costs one catalog entry
  and one SHA-256 over its bytes (the largest measured landing is 372 sectors).
- The pin: `psxport.pin` names a framework commit that exists only in the local psxport repository until the
  framework branch is pushed.
