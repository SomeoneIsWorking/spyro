# Native/Lightrec migration

This is the only Spyro product-execution plan. It follows
`../../../shared/jit-common/docs/migration.md` and replaces the offline-generated-C plan; there is no
static compatibility mode.

## Preserved evidence

Migration changes the execution owner, not recovered binary facts or native subsystem contracts.

- Spyro 1 is `SCUS_942.28`, entry `0x8005B8E0`, game main `0x80012204`, boot owners
  `0x800127C0`/`0x8001286C`, and libetc VSync `0x8005DBC4`.
- The retired product reached stage 13 through an 800-field boot/title route and a 900-field
  forced-input mode-2 save-picker route. Both exercised the native `Spyro1FrameDriver`,
  `FieldScheduler`, native scene owners, and one-fence invariant. Neither proves Lightrec execution or
  representative gameplay.
- A controlled route reached stage 14 and then stage 0; held input moved Spyro and the guest update
  responded to jump, charge, and flame. Visual/oracle parity and complete effect/actor coverage remain
  open.
- Runtime code is loaded from `WAD.WAD`; many images reuse one guest load address. Cache and override
  keys therefore require image generation plus guest address.
- `game/core/world_body.inc` is an emitted guest-body derivative. Its call sites and behavior
  boundaries are evidence for runtime resumption; the file is not a future product component.
- Spyro 2 and Spyro 3 retain their independently measured identities, crt0 facts, VSync addresses,
  and honest execution gaps. Title-specific implementation does not resume until Spyro 1 passes its
  declared gate.

## Target architecture

The launcher authenticates the selected executable and supplies the original bytes to a per-`Core`
psxport executor. Lightrec owns dynamic translation and code-cache memory. psxport owns `Core`
synchronization, PSX service callbacks, bounded exits, image-aware override/original dispatch, and
invalidation. Spyro code owns measured title policy and native subsystems.

An interpreter may exist only in a separately built test/diagnostic target. The gameplay executable
must not link it, expose an engine selector for it, or fall back to it. Static analysis may retain
symbols or other non-executable metadata; it may not emit guest function bodies.

## Ordered migration

1. **Shared executor prerequisite.** Integrate the maintained pinned Lightrec revision directly into
   psxport as a per-`Core` executor. Prove a resident native override, a scoped original call, and two
   address-reusing WAD images, with positive and controlled-negative invalidation.
2. **Spyro runtime dispatch.** Replace generated-symbol registration with executable/WAD-generation
   plus address identity. Route ordinary calls and scoped original calls through Lightrec. Convert
   frame, service, interrupt, exception, and process suspension into explicit executor results.
3. **First discriminator: stage 13 at 800/900 fields.** Run nonzero Lightrec blocks from authenticated
   `SCUS_942.28`; preserve the existing native boot/title/save producers and scheduler; pass the
   800-field boot/title and 900-field mode-2 routes with exactly one presentation fence per host step
   and guest VSync still fatal.
4. **World-body replacement.** Delete `game/core/world_body.inc` and replace every call into that
   derivative with execution of the unchanged retail world body through Lightrec. Suspend at explicit
   host-owned boundaries, deliver the required native service/frame work, and resume the same guest
   CPU state. Do not transcribe the body or retain an emitted super-call.
5. **Representative gameplay gate.** Drive a bounded interactive route beyond stage-13/menu/FMV
   checkpoints. Compare timing, interrupts, memory, and relevant device state with an independent
   emulator or separate test oracle; exercise native and scoped-original calls plus WAD replacement;
   verify correctness and frame-time budgets on every released host; and prove by link/configuration
   inspection that the interpreter is absent and unselectable.
6. **Atomic retirement.** Only after step 5 passes, delete the generator, generated corpora,
   emission-only seed manifests, generated dispatch and symbol tests, offline build/provisioning
   rules, and obsolete documentation. A fresh checkout must build and launch from the player's disc
   without offline translation or a pre-populated runtime cache.
7. **Later titles.** Continue Spyro 2 from `0x80011B1C`, then Spyro 3 from its measured boot boundary,
   only after Spyro 1's compatibility and performance gates pass. Reuse framework mechanics, never
   title addresses or unverified behavior.

## Completion boundary

Passing both stage-13 800/900 routes proves that the replacement executor is wired to real Spyro code.
It does not authorize static-path deletion. The migration completes only after representative
gameplay passes and the native/Lightrec launcher is the sole product path.
