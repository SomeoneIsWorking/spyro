---
id: C164
kind: claim
status: holds
created: 2026-08-06
tags: memcard,bios,framework
depends: external/psxport/runtime/recomp/hle.cpp#dispatchBios
---

## Claim

psxport's Hle::dispatchBios claimed B0:0x35 (FileWrite) for EVERY fd and returned len, so a memory-card WRITE never reached the card at all — memcard.cpp's file_write was dead code for 0x35

## Evidence

PSXPORT_DEBUG=card,bios on the memory-card repro: 3986996 [bios] B0:0x35(fd=3, buf=0x80185BB0, len=0x1400) lines from 0x800672E8 and ZERO [card] lines from memcard.cpp (scratch/mcfix/logs/fix2.log) — the guest's write retry loop spinning against a handler that never touched the card. Source: hle.cpp's B-table 'case 0x35' sat above the 'default: card_hle_b0(fn,c)' arm and did 'if (fd==1||fd==2) fputc(...); c->r[V0]=len; return true'. There is no case 0x34, so READ always reached the card — which is why the read half looked right. Hermetic proof: tests/test_memcard_file_api.cpp drives Hle::dispatchBios (not card_hle_b0) and, before the fix, test_transfer_actually_moves_the_bytes reported same=0 of 128 bytes compared, i.e. the write was discarded while reporting success. After deleting that case so the call falls to card_hle_b0: 6/6 tests, 22 checks. Integration: the same headless repro now writes a real BASCUS-94228SPYRO save onto a blank card image and the game reaches its save-slot menu.

## What would falsify it

a consumer shown to need B0:0x35 to return the byte count on a NON-console fd, or a card write reaching the image while hle.cpp's case 0x35 is restored
