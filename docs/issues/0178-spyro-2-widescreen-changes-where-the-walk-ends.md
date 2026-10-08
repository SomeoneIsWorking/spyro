---
id: 178
title: Spyro 2 widescreen changes where the walk ends
status: open
symptom: the same walk ends at x 69231 at 4:3 and 69112 at 16:9
tags: spyro2,widescreen,determinism
created: 2026-10-08
updated: 2026-10-08
---

Same route and input, `scratch/record/route.py --walk 60`: the player ends at x 69231 at 4:3 and
69112 at 16:9, on the Record path and on the old Gte path alike. On Record the guest's projection
centre and draw area are retail's at both aspects, so the guest-visible difference is elsewhere;
not traced yet. Widescreen must not change guest behaviour, so this is a defect.
