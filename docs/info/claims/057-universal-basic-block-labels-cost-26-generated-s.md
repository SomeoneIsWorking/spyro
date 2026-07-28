---
id: C057
kind: claim
status: holds
created: 2026-07-28
tags: recomp,framework
---

## Claim

Universal basic-block labels cost +26% generated source, +16% binary, ~2x build time — measured, not estimated

## Evidence

Measured on this game with a PSXPORT_LABEL_ALL=1 flag added to emit.py for the purpose (labels every standalone instruction). Baseline: generated .c 5,078,995 bytes, binary 19,498,776 bytes, build 5.0s wall / 18.0s user. With universal labels: generated .c 6,395,908 (+25.9%), binary 22,578,000 (+15.8%), build 10.3s wall / 44.3s user (roughly double wall, 2.5x user). The port behaves identically (same single fail-fast), confirming labels are INERT on their own — which is the point: this measures only the label half of docs/issues/0021 option 1. The other half, entering a function at a computed label, needs a dispatch switch at each function's top and would add further cost on top of these numbers. So the honest figure for the full option is 'at least this much'.

## What would falsify it

A re-measurement on another consumer showing materially different ratios, which would mean the cost is game-shaped rather than general.
