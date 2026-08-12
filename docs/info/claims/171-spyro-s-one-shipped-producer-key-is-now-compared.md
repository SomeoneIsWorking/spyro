---
id: C171
kind: claim
status: holds
created: 2026-08-12
tags: producers,gate
depends: tools/verify_producers.py, tools/gate.sh, game/render/fx_title_menu.cpp#spriteEmit
reconfirmed: 2026-08-12 21:16:05
verified_at: 2026-08-12 21:16:05
---

## Claim

Spyro's one shipped producer KEY is now compared, by code, to the address measured from the guest overlay bytes, and the gate requires the scope to have actually FIRED in a run — so neither a hand-edited constant nor a scope that never runs can ship green

## Evidence

tools/gate.sh 40 (2026-08-12, under gpuguard, 82s) prints 19 PASS / 0 FAIL including the two new checks 'producer keys == measured (+selftest)' and 'producer scope fired in a run  titlefx:spriteEmit 0x8007cd38 prims_native 1380'. The static half derives 0x8007CD38 from OV_5B800 alone (unique lui $r,0x0900 POLY_FT4-tag word at 0x8007CD64, 1 of 3584 words; walk back to the enclosing jr-ra-delimited prologue) and only then reads the constant out of game/render/fx_title_menu.cpp. Sabotage, both directions, real output quoted in the round report: constant -> 0x8007CEE4 gives rc=1 'the port SHIPS 0x8007cee4 but the image MEASURES 0x8007cd38'; the ProducerScope narrowed to close before the push leaves the static half GREEN and turns the --db half rc=1 'reported NO row keyed 0x8007cd38' (scratch/logs/prod_sabotage.log: '0 row(s) ... unscoped-native 1376', with the framework's own warn line). Restored + rebuilt: 1 row, 1378 prims over 696 frames, cross-checked 585x0 + 14x1 + 682x2 = 1378 against the producer's independent titlefx counter (scratch/logs/prod_restored.log).

## What would falsify it

a ProducerScope added to game/ with no measurement recipe in verify_producers.py (the tool REFUSES, exit 2, rather than passing — so this is falsified by someone adding a recipe that restates the shipped constant instead of deriving it); or the fingerprint ceasing to be unique in its overlay, which makes the derivation refuse rather than answer

## Re-confirmed 2026-08-12 21:16:05

VERIFIED by running it, both directions, 2026-08-12 independent pass. Static half GREEN (rc=0, derives 0x8007cd38 from OV_5B800: unique lui $r,0x0900 site at 0x8007cd64, 1 of 3584 words, 56 jal sites). --selftest rc=0, 6/6 mutants caught, no WRONG REASON. MY OWN live sabotage, not the original agent's: (a) real constant sed'd to 0x8007CEE4 -> rc=1 'the port SHIPS 0x8007cee4 but the image MEASURES 0x8007cd38', restored to md5 b52f28f5df66294cb13a62203e3df693; (b) ProducerScope wrapped '{ ... }' so it closes before the push, REBUILT, FULL GATE RUN -> rc=1, '[gate] tally: 17 PASS, 1 FAIL (checks = 18)', FAIL 'producer scope fired in a run' with 'reported NO row keyed 0x8007cd38' while the STATIC half stayed PASS — proving the two halves are genuinely complementary (scratch/logs/V_gate_RED.log). Framework agreed: '1380 native prim(s) drew with NO ProducerScope open'. Restored, md5 identical, clean gate rc=0 '19 PASS, 0 FAIL' (scratch/logs/V_gate.log). ALSO verified the corpus-absent refusal for BOTH invocations (OV_5B800.BIN moved aside): plain rc=2 and --selftest rc=2 with 'NOTHING was tested' — no traceback, so I048's note holds.
