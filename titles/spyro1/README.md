# Spyro the Dragon

First migration title (`SCUS_942.28`, USA). Its native seam currently lives in the repository-level
`game/` and `titles/spyro1/` directories. `Spyro1Runtime`
inherits the lineage `SpyroRuntime`, owns the native lifecycle, and alone binds the remaining
`SCUS_942.28` compatibility views. Provisioning verifies `titles/spyro1/executable.json` before
publishing the executable cache.

The old product compiled offline-emitted guest C; that path is retired. The target product executes
all remaining guest instructions from the authenticated image through Lightrec, with no linked or
selectable interpreter. Its first discriminator reproduces the 800-field boot/title and 900-field
mode-2 save-picker routes and replaces `game/core/world_body.inc` with resumable runtime guest
execution. Representative gameplay is required before the old corpus is deleted.
