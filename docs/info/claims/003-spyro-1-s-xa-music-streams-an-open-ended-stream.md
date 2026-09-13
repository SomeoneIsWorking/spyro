---
id: C003
kind: claim
status: holds
created: 2026-09-14
tags: audio
depends: external/psxport/runtime/psx/xa_stream.cpp
---

## Claim

Spyro 1's XA music streams: an open-ended stream survives a foreign file/channel EOF

## Evidence

docs/project-state.md S022: windowless 3500-field capture decodes 959 XA sectors and carries a real waveform (zero crossings 900-2400/s, RMS 1750-4134), against xa_sectors=0 and a DC-only sink before psxport 379eafea

## What would falsify it

xa_sectors returns to 0 for a driven window, or a foreign EOF ends the open-ended stream again
