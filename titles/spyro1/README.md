# Spyro the Dragon

Current implemented title (`SCUS_942.28`, USA). Its seam, generated substrate, and native work
currently live in the repository-level `game/` and `generated/` directories. `Spyro1Runtime`
inherits the lineage `SpyroRuntime`, owns the native lifecycle, and alone binds the remaining
`SCUS_942.28` compatibility views. Provisioning verifies `titles/spyro1/executable.json` before
publishing the executable cache.
