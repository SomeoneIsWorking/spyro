---
id: C037
kind: claim
status: holds
created: 2026-07-28
tags: gpu
---

## Claim

The guest's render queue was never drained — nothing called rq.flush() on this port's frame path

## Evidence

rq.flush()/emitQueue() are the only consumers of the render queue, and grep shows their sole callers are the framework's native_boot / Engine::drawOTag path, which this port never runs (the guest owns its own frame loop). RenderQueue::push() resets lazily and ONLY when 'consumed' is set, which only mark_consumed() (reached from flush) sets — so with no consumer the queue grows without bound. psxport's own native_boot.cpp documents the identical historical bug with the identical symptom: 'the queue filled every frame and never drained, and NOTHING 2D reached the VK renderer (the whole front-end rendered black)'. Adding c->game->rq.flush(c) before gpu_present in the vblank wait REMOVED the overflow abort: the '[rq:error] render queue full' fail-fast no longer fires and the run advances 3781 -> 3931 frames.

## What would falsify it

The rq:error overflow reappearing, or finding another live caller of rq.flush on this port's path.
