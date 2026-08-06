#!/usr/bin/env python3
"""present_geometry.py — is the presented picture the right SHAPE?

DUPLICATED FILE, AND ONE COPY IS CURRENTLY STALE. It lives in THREE repos:

    spyro/tools/present_geometry.py         this version
    Tomba2Engine/tools/present_geometry.py  byte-identical to this version
    spider1/tools/present_geometry.py       the ORIGINAL, and STALE — it still prints a confident
                                            band-only aspect (see THE CONFOUNDER below). Fix it with
                                            `cp spyro/tools/present_geometry.py spider1/tools/`.

A fix in one MUST be propagated to the others. `md5sum */tools/present_geometry.py` from ~/repo/psx
tells you whether they have drifted — run it before trusting a result from any copy, because two of
them disagree about the same frame today.

This arrangement is a workflow defect, not a design: the file is a game-agnostic PPM/PNG reader with
no game knowledge in it at all, so its correct home is `external/psxport/tools/`, where all three
consumers already share a submodule and would each pick it up at their own pin. Moving it there is a
FRAMEWORK change and therefore needs a `coord/claims/<area>` claim plus an operator commit — which is
why it has not been done here.

WHY THIS EXISTS. Issue 0008 (the picture presented 1.6x too wide for a whole session) was found by
the USER asking "are these stretched wide?" — not by any check in this repo. It could not have been
found by one, and that is the point worth understanding before using this tool:

    EVERY existing instrument here is INVARIANT UNDER THE BUG.
    Non-black coverage %, distinct-colour count, mean brightness, per-tile richness — a uniform
    rescale of the picture changes NONE of them. ppm_look.py reported "real frame, 62.0% non-black,
    528 colours" on a frame stretched 1.6x, and every one of those numbers was correct.

So the gap was not a weak instrument, it was a MISSING DIMENSION: nothing measured geometry. This
measures geometry and nothing else.

WHAT IT DOES. Finds the picture's content band inside the sink (the non-black bounding box), works
out what the PICTURE RECTANGLE could be, and compares its aspect to the aspect the picture SHOULD
have. On PSX every horizontal mode (256/320/368/512/640) scans into the same 4:3 screen area, so
pixels are non-square and the correct presented aspect is 4:3 REGARDLESS of framebuffer width —
unless a widescreen mod is deliberately active, which is what --expect is for.

    python3 tools/present_geometry.py scratch/shots/present_10000.ppm
    python3 tools/present_geometry.py shot.ppm --expect 16:9        # widescreen mod active
    python3 tools/present_geometry.py --selftest                    # prove the tool still fires

THE CONFOUNDER, AND WHAT THIS VERSION DOES ABOUT IT
---------------------------------------------------
The tool measures the NON-BLACK CONTENT BAND. That is NOT the picture rectangle, and the difference
is not cosmetic: Spyro's guest draws only 224 of its 240 display lines, so on a genuinely 1.600x
stretched present the band-only reading is 1.714x. The first version of this file NAMED that hazard
in its docstring and then printed the 1.714 anyway, in the same confident format as a real answer.

The two cases are **not separable from the presented pixels alone**:

    black because the game drew black    -> INSIDE the picture rect
    black because it is a letterbox bar  -> OUTSIDE the picture rect

Both are "a run of black rows touching the frame edge". No pixel-level test tells them apart. So
this version does two things instead of guessing:

 1. WITHOUT extra information it reports an INTERVAL, not a number. The band bounds the picture rect
    from below and the sink bounds it from above (band <= picture <= sink), so the aspect lies in
    [band_w/sink_h, sink_w/band_h]. If that whole interval fails the expectation the verdict is
    still confident (STRETCHED/SQUASHED, rc 1). If the whole interval passes, OK (rc 0). If it
    STRADDLES the accept window the tool REFUSES with rc 3 and says exactly what to feed it. It no
    longer prints 1.714 for a 1.600 frame; it prints "cannot tell, here is the range, here is why".

 2. WITH the frame's own drawn extent it computes the picture rect exactly:

        --active 512x224 --display 512x240      # drawn bbox / display rect, in GUEST pixels
        --guest-frame scratch/shots/vram.png --display 512x240    # measure the drawn bbox for me

    The present is a uniform scale of the guest display rect, so
    picture_w = band_w * display_w/active_w and picture_h = band_h * display_h/active_h. The
    caller derives both numbers FROM THE FRAME (dump the guest framebuffer, measure its non-black
    bbox) rather than hardcoding a ratio. --guest-frame does that measurement for you with the same
    code path that measures the present, so the two cannot disagree by construction.

    `--active` is a CALLER ASSERTION about the guest side, and the tool checks it: if the implied
    picture rect does not fit in the sink, the inputs are inconsistent and it refuses (rc 2) rather
    than reporting the impossible rectangle.

WHAT IT STILL CANNOT DO, stated so its silence is never mistaken for a pass:
  * It CANNOT distinguish "the picture is correctly 4:3" from "the picture is square and the game
    happens to be drawing a square thing". It is a geometry check on the frame, not a check that the
    frame's CONTENT is undistorted.
  * The --active correction assumes the present is a UNIFORM SCALE of the guest display rect. If the
    presenter crops, pans, or scales the axes by different amounts *inside* the picture rect, the
    correction is wrong and the tool cannot detect it.
  * The interval assumes only band <= picture <= sink. It does NOT assume the picture is centred in
    the sink, so it is deliberately loose. A centred-letterbox assumption would tighten it and would
    also be an assumption about the presenter that this tool has no way to verify.
  * A fully-black frame has no measurable geometry at all. It REFUSES (rc 2) rather than returning
    an aspect, because "0x0 band" would otherwise compare unequal to everything and read as a
    detected bug.

USING IT ON REAL CAPTURES — one hygiene rule that has already cost a session. `scratch/screenshots/`
is a SHARED ACCUMULATOR that every run in every tree writes into. Do NOT glob it. A stale file swept
in by a glob was read as "the correct reference frame" and manufactured a complete false root cause
for spider1's flicker. Capture into a per-run directory and check each file against its own
`present_shot` log line before measuring it. This tool reports the path it measured on its first
output line precisely so that check is possible.

EXIT CODES (the max severity over all inputs):
  0  OK — the picture rect is the expected aspect, within --tol.
  1  STRETCHED or SQUASHED — confidently the wrong shape.
  2  REFUSED — no measurable geometry (all-black), unreadable input, or inconsistent --active.
  3  REFUSED — AMBIGUOUS: the band has margins and no --active/--guest-frame was given, so the
     aspect could be right or wrong and the tool will not pick one.

INPUTS: binary PPM (P6) and PNG (8-bit truecolour RGB/RGBA, non-interlaced). Anything else is
refused by name — a silently mis-decoded image would produce a confident wrong rectangle.
"""
import sys
import zlib

# --------------------------------------------------------------------------------------------
# readers
# --------------------------------------------------------------------------------------------


class Unreadable(Exception):
    """Input we will not guess at. Always raised with the reason, never swallowed."""


def read_ppm(path, d):
    parts, i = [], 0
    while len(parts) < 4:
        while i < len(d) and d[i:i + 1].isspace():
            i += 1
        if i < len(d) and d[i:i + 1] == b'#':                 # comment line
            while i < len(d) and d[i:i + 1] != b'\n':
                i += 1
            continue
        j = i
        while j < len(d) and not d[j:j + 1].isspace():
            j += 1
        parts.append(d[i:j])
        i = j
    i += 1
    if parts[0] != b'P6':
        raise Unreadable('%s is not a P6 PPM (magic %r)' % (path, parts[0]))
    w, h = int(parts[1]), int(parts[2])
    px = d[i:i + w * h * 3]
    if len(px) != w * h * 3:
        raise Unreadable('%s is truncated: %d pixel bytes, expected %d'
                         % (path, len(px), w * h * 3))
    return w, h, px


def read_png(path, d):
    """8-bit truecolour, non-interlaced only. Every other PNG shape is REFUSED BY NAME rather than
    decoded approximately: shot.py's reader, for one, treats a palette image's indices as grey, and
    a mis-decoded image here yields a confident wrong rectangle instead of an error."""
    i, idat, hdr = 8, [], None
    while i + 8 <= len(d):
        ln = int.from_bytes(d[i:i + 4], 'big')
        typ, dat = d[i + 4:i + 8], d[i + 8:i + 8 + ln]
        if typ == b'IHDR':
            hdr = (int.from_bytes(dat[0:4], 'big'), int.from_bytes(dat[4:8], 'big'),
                   dat[8], dat[9], dat[12])          # w, h, bitdepth, colourtype, interlace
        elif typ == b'IDAT':
            idat.append(dat)
        elif typ == b'IEND':
            break
        i += 12 + ln
    if hdr is None:
        raise Unreadable('%s: PNG has no IHDR' % path)
    w, h, bd, ct, il = hdr
    if bd != 8 or ct not in (2, 6) or il != 0:
        raise Unreadable('%s: PNG is bitdepth %d, colourtype %d, interlace %d — this reader handles '
                         'only 8-bit truecolour RGB(2)/RGBA(6), non-interlaced. REFUSING rather '
                         'than decoding it wrongly.' % (path, bd, ct, il))
    ch = 3 if ct == 2 else 4
    raw = zlib.decompress(b''.join(idat))
    stride = w * ch
    out, prev, pos = bytearray(), bytearray(stride), 0
    for _ in range(h):
        f = raw[pos]; pos += 1
        line = bytearray(raw[pos:pos + stride]); pos += stride
        if f == 1:
            for x in range(ch, stride):
                line[x] = (line[x] + line[x - ch]) & 255
        elif f == 2:
            for x in range(stride):
                line[x] = (line[x] + prev[x]) & 255
        elif f == 3:
            for x in range(stride):
                a = line[x - ch] if x >= ch else 0
                line[x] = (line[x] + ((a + prev[x]) >> 1)) & 255
        elif f == 4:
            for x in range(stride):
                a = line[x - ch] if x >= ch else 0
                c = prev[x - ch] if x >= ch else 0
                b = prev[x]
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else (b if pb <= pc else c))) & 255
        elif f != 0:
            raise Unreadable('%s: PNG row filter %d is not valid' % (path, f))
        prev = line
        if ch == 3:
            out += line
        else:
            for x in range(0, stride, 4):
                out += line[x:x + 3]
    return w, h, bytes(out)


def read_image(path):
    d = open(path, 'rb').read()
    if d[:8] == b'\x89PNG\r\n\x1a\n':
        return read_png(path, d)
    if d[:2] == b'P6':
        return read_ppm(path, d)
    raise Unreadable('%s: not a P6 PPM and not a PNG (first bytes %r)' % (path, d[:8]))


# --------------------------------------------------------------------------------------------
# geometry
# --------------------------------------------------------------------------------------------


def content_band(w, h, px, thresh=0):
    """Bounding box of pixels brighter than `thresh`, as (x, y, w, h), or None if nothing is
    brighter. Full scan — no subsampling, because a subsampled scan can miss a one-pixel border and
    silently shrink the band. The per-row work is done with bytes.lstrip/rstrip so a full-resolution
    scan of a 960x720 sink is still instant."""
    tbl = None if thresh == 0 else bytes(0 if v <= thresh else 1 for v in range(256))
    x0, x1, y0, y1 = w, -1, -1, -1
    stride = w * 3
    for y in range(h):
        row = px[y * stride:(y + 1) * stride]
        if tbl is not None:
            row = row.translate(tbl)
        end = len(row.rstrip(b'\x00'))
        if end == 0:
            continue                                   # entirely black row
        lead = len(row) - len(row.lstrip(b'\x00'))
        if y0 < 0:
            y0 = y
        y1 = y
        fx, lx = lead // 3, (end - 1) // 3
        if fx < x0:
            x0 = fx
        if lx > x1:
            x1 = lx
    if y1 < 0:
        return None
    return x0, y0, x1 - x0 + 1, y1 - y0 + 1


def parse_aspect(s):
    if ':' in s:
        a, b = s.split(':', 1)
        return float(a) / float(b)
    return float(s)


def parse_wh(s, what):
    t = s.lower().replace('*', 'x').split('x')
    if len(t) != 2:
        raise Unreadable('%s wants WxH like 512x224, got %r' % (what, s))
    return int(t[0]), int(t[1])


def verdict_word(ratio):
    if ratio > 1:
        return 'STRETCHED %.3fx WIDE' % ratio
    return 'SQUASHED %.3fx (too narrow)' % ratio


# --------------------------------------------------------------------------------------------
# report
# --------------------------------------------------------------------------------------------


def report(path, w, h, px, expect, expect_s, tol, active, display, out=print):
    """Returns the exit code for this one file and prints the whole report. `active`/`display` are
    (w, h) in GUEST pixels or None."""
    band = content_band(w, h, px)
    out('%s' % path)
    out('  sink        %dx%d' % (w, h))
    if band is None:
        # A black frame has NO geometry. Refusing is the whole point: returning 0x0 here would
        # compare unequal to the expected aspect and print a confident "STRETCHED" for a frame that
        # simply has no picture in it.
        out('  content     NONE — every pixel is black (scanned all %d rows x %d cols)' % (h, w))
        out('  verdict     REFUSED: entirely black, no measurable geometry. This is not a pass and '
            'not a fail.')
        return 2
    bx, by, bw, bh = band
    out('  content     x %d..%d (%d wide), y %d..%d (%d tall)'
        % (bx, bx + bw - 1, bw, by, by + bh - 1, bh))
    # The band touching every edge means the picture fills the sink; margins mean it does not, and
    # a margin is exactly the ambiguity this tool refuses to guess through. Reported explicitly
    # because "big black bars" is what an aspect bug looks like to a human, and a reader who sees
    # bars tends to assume they are deliberate letterboxing.
    margins = []
    if by > 0 or by + bh < h:
        margins.append('top/bottom %d+%d px' % (by, h - (by + bh)))
    if bx > 0 or bx + bw < w:
        margins.append('left/right %d+%d px' % (bx, w - (bx + bw)))
    out('  margins     %s' % (', '.join(margins) if margins else 'none (band fills the sink)'))

    lo_ok, hi_ok = expect * (1.0 - tol), expect * (1.0 + tol)

    if active and display:
        aw, ah = active
        dw, dh = display
        if aw <= 0 or ah <= 0 or dw <= 0 or dh <= 0:
            out('  verdict     REFUSED: --active/--display must be positive.')
            return 2
        if aw > dw or ah > dh:
            out('  verdict     REFUSED: drawn extent %dx%d does not fit the display rect %dx%d — '
                'the guest cannot draw outside its own display rect, so one of these is wrong.'
                % (aw, ah, dw, dh))
            return 2
        pw = bw * (dw / float(aw))
        ph = bh * (dh / float(ah))
        out('  guest       drawn %dx%d of display rect %dx%d  -> band is %.1f%% x %.1f%% of the '
            'picture' % (aw, ah, dw, dh, 100.0 * aw / dw, 100.0 * ah / dh))
        out('  picture     %.1f x %.1f  (band scaled up by the undrawn guest margin)' % (pw, ph))
        if pw > w + 1.5 or ph > h + 1.5:
            out('  verdict     REFUSED: that picture rect does not fit inside the %dx%d sink. The '
                '--active/--display you gave is inconsistent with this frame; refusing to report '
                'an impossible rectangle.' % (w, h))
            return 2
        got = pw / ph
        out('  aspect      %.3f:1   expected %s = %.3f:1' % (got, expect_s, expect))
        if lo_ok <= got <= hi_ok:
            out('  verdict     OK')
            return 0
        out('  verdict     %s' % verdict_word(got / expect))
        return 1

    # No guest-side information. band <= picture <= sink is ALL we know, so the aspect is an
    # interval, not a number.
    lo = bw / float(h)          # widest possible picture height, narrowest possible width
    hi = w / float(bh)          # narrowest possible picture height, widest possible width
    out('  aspect      %.3f .. %.3f : 1   expected %s = %.3f:1'
        % (lo, hi, expect_s, expect))
    if lo > hi_ok or hi < lo_ok:
        got = lo if lo > hi_ok else hi
        why = ('' if not margins else
               '  (the ENTIRE range is off, so this holds however the black margins are apportioned '
               'between picture and letterbox)')
        out('  verdict     %s%s' % (verdict_word(got / expect), why))
        return 1
    if lo >= lo_ok and hi <= hi_ok:
        out('  verdict     OK')
        return 0
    naive = bw / float(bh)
    out('  verdict     REFUSED: AMBIGUOUS. The band has black margins, and black INSIDE the picture')
    out('              (the game drew it) is indistinguishable from black OUTSIDE it (a letterbox')
    out('              bar). The true aspect is anywhere in the range above, and that range spans')
    out('              the expected %.3f, so both "right" and "wrong" are consistent with these'
        % expect)
    out('              pixels. Band-only arithmetic would have said %.3f:1 = %.3fx — that is the'
        % (naive, naive / expect))
    out('              number this tool used to print here, and it is NOT trustworthy.')
    out('              To get a verdict, tell it the frame\'s own drawn extent:')
    out('                --active <drawn WxH> --display <guest display WxH>')
    out('                --guest-frame <guest framebuffer dump> --display <guest display WxH>')
    return 3


# --------------------------------------------------------------------------------------------
# self-test
# --------------------------------------------------------------------------------------------


def _ppm(w, h, fill_rows=None, fill_cols=None, colour=(40, 90, 200)):
    """A synthetic P6 frame: black everywhere except rows fill_rows[0]..[1] and cols
    fill_cols[0]..[1] inclusive (defaults: the whole frame)."""
    r0, r1 = fill_rows if fill_rows else (0, h - 1)
    c0, c1 = fill_cols if fill_cols else (0, w - 1)
    hdr = ('P6\n%d %d\n255\n' % (w, h)).encode()
    black = bytes(w * 3)
    lit = bytearray(black)
    for x in range(c0, c1 + 1):
        lit[x * 3:x * 3 + 3] = bytes(colour)
    body = b''.join(bytes(lit) if r0 <= y <= r1 else black for y in range(h))
    return hdr + body


def selftest():
    """Feeds the tool cases that MUST produce each of its four answers, including the exact shape of
    the confounder it exists to refuse. Frames go to the repo's gitignored scratch/ (never /tmp,
    which is a small RAM-backed tmpfs here) and are overwritten each run, so they do not accumulate.
    Everything is half the real sink size — the arithmetic is identical and the files are 1/4 the
    bytes."""
    import os
    d = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                     'scratch', 'present_geometry_selftest')
    os.makedirs(d, exist_ok=True)
    cases = []

    def frame(name, data):
        p = os.path.join(d, name)
        open(p, 'wb').write(data)
        return p

    # 1. a correct 4:3 present that fills its sink                              -> OK, rc 0
    cases.append(('correct 4:3, band fills sink', frame('ok43.ppm', _ppm(480, 360)),
                  None, None, 0, 'OK'))
    # 2. the SAME picture presented 1.600x too wide into a sink it fills        -> STRETCHED, rc 1
    cases.append(('stretched 1.600x, band fills sink', frame('wide.ppm', _ppm(480, 225)),
                  None, None, 1, 'STRETCHED'))
    # 3. a frame with no picture in it at all                                   -> REFUSED, rc 2
    cases.append(('entirely black', frame('black.ppm', _ppm(480, 360, (0, -1))),
                  None, None, 2, 'REFUSED'))
    # 4. THE CONFOUNDER, uncorrected. Spyro's real shape: a 1.600x stretched 480x225 picture rect
    #    inside a 480x360 sink, of which the guest draws only 224/240 lines -> a 480x210 band.
    #    Band-only arithmetic gives 480/210 / (4/3) = 1.714x. It must NOT say that.
    spyro_broken = frame('spyro_broken.ppm', _ppm(480, 360, (75, 284)))
    cases.append(('spyro-shaped stretch, NO guest info', spyro_broken, None, None, 3, 'AMBIGUOUS'))
    # 5. the same frame WITH the frame's own drawn extent                       -> STRETCHED 1.600x
    cases.append(('spyro-shaped stretch, WITH --active', spyro_broken,
                  (512, 224), (512, 240), 1, 'STRETCHED 1.600x'))
    # 6. NEGATIVE CONTROL for case 5: the FIXED present (picture rect 480x360, still only 224/240
    #    lines drawn -> a 480x336 band), same tool, same flags.                 -> OK, rc 0
    cases.append(('spyro-shaped FIXED, WITH --active', frame('spyro_fixed.ppm',
                                                             _ppm(480, 360, (12, 347))),
                  (512, 224), (512, 240), 0, 'OK'))
    # 7. --active that cannot fit the sink is an inconsistent assertion         -> REFUSED, rc 2
    cases.append(('inconsistent --active', spyro_broken,
                  (512, 60), (512, 240), 2, 'REFUSED'))
    # 8. pillarboxed: a 4:3 picture in a 16:9 sink, band fills the picture. Range is
    #    [480/360, 640/360] = [1.333, 1.778], which straddles 4:3.              -> AMBIGUOUS, rc 3
    cases.append(('4:3 picture in a 16:9 sink', frame('pillar.ppm',
                                                      _ppm(640, 360, None, (80, 559))),
                  None, None, 3, 'AMBIGUOUS'))
    # 9. squashed beyond any doubt: a 240x360 band in a 240x360 sink            -> SQUASHED, rc 1
    cases.append(('squashed, band fills sink', frame('tall.ppm', _ppm(240, 360)),
                  None, None, 1, 'SQUASHED'))

    bad = 0
    for name, path, act, disp, want_rc, want_txt in cases:
        lines = []
        w, h, px = read_image(path)
        rc = report(path, w, h, px, 4.0 / 3.0, '4:3', 0.02, act, disp, out=lines.append)
        txt = '\n'.join(lines)
        ok = (rc == want_rc) and (want_txt in txt)
        bad += 0 if ok else 1
        print('%-6s %-38s rc=%d (want %d), expected text %r %s'
              % ('PASS' if ok else 'FAIL', name, rc, want_rc, want_txt,
                 '' if ok else '\n' + txt))
    # 10. --guest-frame must derive the same drawn extent that case 5 was told by hand.
    gp = frame('guest.ppm', _ppm(512, 240, (8, 231)))
    gw, gh, gpx = read_image(gp)
    gb = content_band(gw, gh, gpx)
    ok = gb == (0, 8, 512, 224)
    bad += 0 if ok else 1
    print('%-6s %-38s measured %s (want (0, 8, 512, 224))'
          % ('PASS' if ok else 'FAIL', '--guest-frame drawn-extent probe', gb))
    # 11. the readers must REFUSE what they cannot decode, not guess.
    junk = frame('junk.bin', b'not an image at all')
    try:
        read_image(junk)
        print('FAIL   unreadable input was accepted'); bad += 1
    except Unreadable as e:
        print('PASS   unreadable input refused by name          %s' % e)
    # 12. PNG round-trip through EVERY row filter. A decoder that is subtly wrong on Average or
    #     Paeth would shift pixel values and could move the content bbox by a row or a column —
    #     silently, since the output would still look like an image. So encode with each filter and
    #     demand the decoded bytes come back identical.
    #
    #     THE SOURCE DATA IS PSEUDORANDOM ON PURPOSE, and this was MEASURED, not assumed. The first
    #     version of this check used a smooth gradient; an injected off-by-one in the Paeth
    #     predictor (`p = a + b - c + 1`) passed it 5/5. Paeth's output is always one of a/b/c, never
    #     p itself, so on smooth data the perturbed distances still select the same neighbour — the
    #     mutant decoded the gradient BYTE-IDENTICALLY (0 of 1173 bytes differed). On pseudorandom
    #     data the same mutant differs in 250 of 1173 bytes and the check fails. Smooth test data
    #     simply cannot reach the predictor's selection boundaries.
    gw, gh = 23, 17
    seed, buf = 12345, bytearray()
    for _ in range(gw * gh * 3):
        seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF
        buf.append((seed >> 16) & 255)
    orig = bytes(buf)
    for f in range(5):
        stride = gw * 3
        rows = []
        prev = bytes(stride)
        for y in range(gh):
            cur = orig[y * stride:(y + 1) * stride]
            enc = bytearray(stride)
            for x in range(stride):
                a = cur[x - 3] if x >= 3 else 0
                b = prev[x]
                c = prev[x - 3] if x >= 3 else 0
                if f == 0:
                    p = 0
                elif f == 1:
                    p = a
                elif f == 2:
                    p = b
                elif f == 3:
                    p = (a + b) >> 1
                else:
                    q = a + b - c
                    pa, pb, pc = abs(q - a), abs(q - b), abs(q - c)
                    p = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                enc[x] = (cur[x] - p) & 255
            rows.append(bytes([f]) + bytes(enc))
            prev = cur

        def chunk(t, b):
            import struct
            return struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b) & 0xFFFFFFFF)
        import struct as _s
        png = (b'\x89PNG\r\n\x1a\n'
               + chunk(b'IHDR', _s.pack('>IIBBBBB', gw, gh, 8, 2, 0, 0, 0))
               + chunk(b'IDAT', zlib.compress(b''.join(rows), 6)) + chunk(b'IEND', b''))
        rw, rh, rpx = read_png('filter%d' % f, png)
        ok = (rw, rh, rpx) == (gw, gh, orig)
        bad += 0 if ok else 1
        print('%-6s PNG row filter %d round-trip%s'
              % ('PASS' if ok else 'FAIL', f,
                 '' if ok else '   DECODED BYTES DIFFER FROM SOURCE'))

    print('\n%d/%d checks passed%s' % (16 - bad, 16, '' if bad == 0 else '   <-- SELFTEST FAILED'))
    return 0 if bad == 0 else 1


# --------------------------------------------------------------------------------------------


def main():
    # Consume option VALUES properly. The first version filtered on a leading "--", which left the
    # value of --expect (e.g. "16:9") in the positional list, and the tool then tried to open it as
    # a PPM. It crashed loudly rather than silently, which is the only reason it was caught in the
    # same minute it was written — but an argument parser that mistakes a flag's value for an input
    # is exactly how a tool ends up measuring the wrong file and reporting it with confidence.
    expect, expect_s, tol = 4.0 / 3.0, '4:3', 0.02
    active = display = guest_frame = None
    args, rest = [], list(sys.argv[1:])
    while rest:
        a = rest.pop(0)
        if a in ('--expect', '--tol', '--active', '--display', '--guest-frame'):
            if not rest:
                print('%s needs a value' % a); return 2
            v = rest.pop(0)
            if a == '--expect':
                expect_s, expect = v, parse_aspect(v)
            elif a == '--tol':
                tol = float(v)
            elif a == '--active':
                active = parse_wh(v, '--active')
            elif a == '--display':
                display = parse_wh(v, '--display')
            else:
                guest_frame = v
        elif a == '--selftest':
            return selftest()
        elif a.startswith('--'):
            print('unknown option %s' % a); return 2
        else:
            args.append(a)
    if not args:
        print(__doc__)
        return 2

    if guest_frame:
        gw, gh, gpx = read_image(guest_frame)
        gb = content_band(gw, gh, gpx)
        if gb is None:
            print('%s: the guest frame is ENTIRELY BLACK, so it has no drawn extent to correct '
                  'with. REFUSING.' % guest_frame)
            return 2
        active = (gb[2], gb[3])
        if display is None:
            display = (gw, gh)
        print('guest frame  %s: %dx%d, drawn extent %dx%d at (%d,%d)  -> --active %dx%d'
              % (guest_frame, gw, gh, gb[2], gb[3], gb[0], gb[1], gb[2], gb[3]))
    if (active is None) != (display is None):
        print('--active and --display go together: --active is the DRAWN bbox and --display is the '
              'guest DISPLAY RECT, both in guest pixels. One without the other says nothing.')
        return 2

    rc = 0
    for path in args:
        w, h, px = read_image(path)
        rc = max(rc, report(path, w, h, px, expect, expect_s, tol, active, display))
    return rc


if __name__ == '__main__':
    try:
        sys.exit(main())
    except Unreadable as e:
        print('REFUSED: %s' % e)
        sys.exit(2)
