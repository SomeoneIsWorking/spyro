# Native/Lightrec migration

This is the only Spyro product-execution plan. It defines the dynamic-runtime migration and replaces
the retired source-emission path; there is no static compatibility mode.

## Preserved evidence

Migration changes the execution owner, not recovered binary facts or native subsystem contracts.

- Spyro 1 is `SCUS_942.28`, entry `0x8005B8E0`, game main `0x80012204`, boot owners
  `0x800127C0`/`0x8001286C`, and libetc VSync `0x8005DBC4`.
- The retired product reached stage 13 through an 800-field boot/title route and a 900-field
  forced-input mode-2 save-picker route. Those runs established title-local field responsibilities
  and the one-fence invariant. Their retired frame/scheduler integrations do not prove Lightrec
  execution or representative gameplay.
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

Interpreter-only execution belongs in a separately built test/diagnostic target. The gameplay
executable always offers code to Lightrec first and exposes no interpreter selector. Only the shared
framework's classified, bounded, accounted fallback after a JIT refusal is permitted. Static analysis
may retain symbols or other non-executable metadata; it may not emit guest function bodies.

## Ordered migration

1. **Break-first retirement — complete in the working tree.** The offline generator, emitted corpus,
   seed manifests, static dispatch registration, static-only tests, selector/build rules,
   `world_body.inc`, and its generated native transcription are absent. The launcher provisions only
   authenticated executable bytes; no old gameplay product remains buildable or selectable.
2. **Shared executor prerequisite — linked and contract-tested.** psxport exposes per-`Core`
   execution, image-aware native dispatch, scoped original calls, invalidation, and typed exits. The
   frozen Lightrec backend is linked and its synthetic framework contract passes. Prove a resident
   override, original call, and two address-reusing WAD images with positive and controlled-negative
   invalidation through a real Spyro media route.
3. **Spyro runtime dispatch — wired to the boundary.** The product enters authenticated crt0 through
   `dispatchGuest`. `WorldGuestExecution` resumes unchanged retail `RenderWorldChunks` at
   `0x800258F0` through `callOriginal`; it neither transcribes nor links a guest body.
4. **First discriminator: stage 13 at 800/900 fields.** Run nonzero Lightrec blocks from authenticated
   `SCUS_942.28`; implement new image-aware frame/field composition around the retained semantic
   recipes; pass the 800-field boot/title and 900-field mode-2 routes with exactly one presentation
   fence per host step and guest VSync still fatal.
5. **Representative gameplay gate.** Drive a bounded interactive route beyond stage-13/menu/FMV
   checkpoints. Compare timing, interrupts, memory, and relevant device state with an independent
   emulator or separate test oracle; exercise native and scoped-original calls plus WAD replacement;
   verify correctness and frame-time budgets on every released host; and prove by link/configuration
   inspection that interpreter-only execution is unselectable and fallback admission remains bounded.
6. **Later titles.** Continue Spyro 2 from `0x80011B1C`, then Spyro 3 from its measured boot boundary,
   only after Spyro 1's compatibility and performance gates pass. Reuse framework mechanics, never
   title addresses or unverified behavior.

## Completion boundary

The destructive half of the migration is complete; the product now links the frozen Lightrec
executor. Real Spyro 1 execution now resumes cycle-budget yields with its original root return
boundary intact; the measured interval and remaining CD synchronization failure are recorded in
`docs/project-state.md` (S008). Passing both stage-13 routes
will prove that the replacement executor is wired to real Spyro code, while completion still requires
representative gameplay, cache/override conformance, and host performance evidence.
