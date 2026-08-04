#!/usr/bin/env python3
"""logsig — extract the MESSAGE TEMPLATE of every diagnostic call site in a C/C++ file.

WHY: converting ~700 `cfg_logf("cd", "sector %u -> %08X", ...)` sites to
`lucent::debug("cd", "sector {} -> {:08X}", ...)` is mechanical, which is exactly why it silently
loses lines. This normalises BOTH spellings to the same canonical string, so a before/after diff over
the sweep is empty if and only if every site survived with the same channel, the same level and the
same message shape.

A STATIC comparison on purpose: a game run exercises a small fraction of the call sites, so an
"identical output" claim from a run has a denominator of maybe 40 templates out of 700. This one has
a denominator of every site in the file, and prints it.

WHAT IT CANNOT SEE (say it, do not let a clean diff imply more than it covers):
  * a call whose format string is not a literal (a variable, a macro, a ternary) — counted and
    reported as `<non-literal>`, never silently dropped;
  * whether the ARGUMENTS still match the placeholders (a wrong argument order compiles and formats);
  * a site deleted together with the code around it — that is a real change and it WILL show, which
    is the point, but it is on you to say it was intended.

USAGE
    logsig.py <file>...                 # one canonical line per call site, sorted
    logsig.py --selftest                # prove the extractor fires (see below)
"""
import re
import sys

CALL = re.compile(
    r'\b(?:'
    r'(?P<cfg>cfg_logf|cfg_logi|cfg_logw|cfg_loge)'
    r'|lucent::(?P<lu>debug|info|warn|error)'
    r')\s*\(', re.S)

LEVEL = {'cfg_logf': 'debug', 'cfg_logi': 'info', 'cfg_logw': 'warn', 'cfg_loge': 'error',
         'debug': 'debug', 'info': 'info', 'warn': 'warn', 'error': 'error'}

# The piecewise line builders. These were INVISIBLE to the scan until the SBS sweep pointed out that
# a clean diff was understating its denominator by 20 sites: `CfgLine`'s cfg_line_addf and its
# replacement `lucent::Line::add` build a row a piece at a time, and each piece carries a format
# string of its own. Neither names a channel at the piece (the channel is on the flush), so these are
# reported with a '-' channel and a 'line' level; what they PROVE is that the pieces and their
# templates survived the rewrite in the same order.
#   cfg_line_addf(&l, fmt, ...)  -> the format is argument 1
#   row.add(fmt, ...)            -> the format is argument 0
# `.add(` is matched only when a string literal follows immediately, so an unrelated container's
# add() cannot be mistaken for a row piece.
LINE_ADD = re.compile(r'\b(?:(?P<cfgline>cfg_line_addf)\s*\(|(?P<luline>\.add)\s*\(\s*(?="))')

# printf conversion -> the std::format spelling this project converts it to. Order matters: the
# longest/most specific patterns first, so "%08X" is not eaten by "%X".
# NO SPACE in the flag class. printf's space flag is legal but unused here, and including it made
# the regex read the "% p" inside an ALREADY-CONVERTED message ("{:.2f}% pixels differ") as a printf
# %p conversion — so a correct conversion showed as a diff. Reported by the SBS sweep.
SPEC = re.compile(r'%(?P<flags>[-+#0]*)(?P<width>\*|\d+)?(?:\.(?P<prec>\*|\d+))?'
                  r'(?P<len>hh|h|ll|l|j|z|t|L)?(?P<conv>[diouxXeEfgGaAcspn%])')


def canon_spec(m):
    """The std::format spelling of one printf conversion, by this project's stated mapping."""
    conv, flags, width, prec = m.group('conv'), m.group('flags') or '', m.group('width'), m.group('prec')
    if conv == '%':
        return '%'
    fill = ''
    if '-' in flags:
        fill = '<'
    elif '0' in flags:
        fill = '0>'
    body = ''
    if width:
        body = (fill or '') + width
    elif fill == '0>':
        body = ''
    if prec:
        body += '.' + prec
    if conv in 'xXdiou':
        # "%08X" -> "{:08X}": the project writes the zero-pad as 0<width>, not 0><width>.
        # Width and the '-' flag matter for the PLAIN integer conversions too, and missing that was a
        # real hole: "%-4u" canonicalised to "{}" while its correct std::format spelling "{:<4}"
        # canonicalised to itself, so a CORRECT conversion showed up as a diff. Reported by the
        # ui/input sweep; the conversion was right and the instrument was wrong.
        w = ('0' + width) if (width and '0' in flags) else (width or '')
        body = (('<' + width) if (width and '-' in flags) else w)
        # '+' forces the sign into the OUTPUT ("+3" vs "3"), so it is part of the template, not
        # decoration: %+d must canonicalise to {:+}, never to {}. Reported by the SBS sweep, where
        # canonicalising it away would have silently dropped the sign from every signed A-B delta.
        sign = '+' if '+' in flags else ''
        suffix = conv if conv in 'xX' else ''
        return ('{:' + sign + body + suffix + '}') if (sign or body or suffix) else '{}'
    if conv in 'eEfgGaA':
        return '{:' + (('.' + prec) if prec else '') + conv.replace('f', 'f') + '}' if prec else '{}'
    if conv == 's':
        if width:
            return '{:' + (('<' + width) if '-' in flags else ('>' + width)) + '}'
        return '{}'
    if conv == 'c':
        return '{}'
    if conv == 'p':
        return '{}'
    return '{}'   # d i o u and anything else: plain


BRACE = re.compile(r'\{\{|\}\}')


def canon_fmt(s, printf):
    """Canonicalise ONE format string. `printf` says which spelling it is written in.

    THE MODEL, and it took three wrong answers to get here: a printf string and its std::format
    twin are canonicalised by DIFFERENT rules, because the same characters mean different things in
    the two languages. Running the printf spec regex over an already-converted std::format string is
    what produced every false diff this tool has reported:

      * "p%%04d.ppm" is a printf-escaped LITERAL "p%04d.ppm" — a filename pattern printed for a
        human. Its correct std::format spelling is the plain "p%04d.ppm", and re-parsing that as a
        printf conversion turned a CORRECT conversion into "p{:04}.ppm" and called it a diff.
      * "{:.2f}% pixels" likewise: "% p" is not a %p conversion.
      * "{native,oracle}" in printf must be written "{{native,oracle}}" in std::format. Only the
        format side has doubled braces to undo.

    So: printf strings get their conversions rewritten into format spelling; format strings get their
    doubled braces undone. Both land on the same string, which is close to what a reader would SEE.

    KNOWN FALSE-EQUAL (small, and stated rather than hidden): a printf string containing a literal
    "{}" canonicalises the same as a placeholder. No call site in this tree does that.
    """
    if printf:
        return SPEC.sub(canon_spec, s)
    return BRACE.sub(lambda m: m.group(0)[0], s)


STR = re.compile(r'"((?:[^"\\]|\\.)*)"')


def split_top(args):
    """Split a call's argument text on top-level commas."""
    out, depth, cur, i, n = [], 0, [], 0, len(args)
    instr = False
    while i < n:
        c = args[i]
        if instr:
            cur.append(c)
            if c == '\\':
                if i + 1 < n:
                    cur.append(args[i + 1]); i += 2; continue
            elif c == '"':
                instr = False
            i += 1
            continue
        if c == '"':
            instr = True; cur.append(c); i += 1; continue
        if c in '([{':
            depth += 1
        elif c in ')]}':
            depth -= 1
        if c == ',' and depth == 0:
            out.append(''.join(cur)); cur = []; i += 1; continue
        cur.append(c); i += 1
    out.append(''.join(cur))
    return out


def literal(expr):
    """Concatenated string literals -> their text; None if the expression is not a literal."""
    parts = STR.findall(expr)
    if not parts:
        return None
    # Reject "x" prefixed/suffixed by something that is not whitespace or another literal.
    stripped = STR.sub('', expr).strip()
    if stripped:
        return None
    return ''.join(parts)


def close_paren(src, start):
    """Index just past the ')' matching the '(' that `start` sits after."""
    i, depth, instr = start, 1, False
    while i < len(src) and depth:
        c = src[i]
        if instr:
            if c == '\\':
                i += 2; continue
            if c == '"':
                instr = False
        elif c == '"':
            instr = True
        elif c == '(':
            depth += 1
        elif c == ')':
            depth -= 1
        i += 1
    return i


def sigs(path):
    src = open(path, encoding='utf-8', errors='replace').read()
    out, nonliteral = [], 0
    for m in LINE_ADD.finditer(src):
        end = close_paren(src, m.end())
        args = split_top(src[m.end():end - 1])
        fmt = literal(args[1]) if m.group('cfgline') and len(args) > 1 else (
              literal(args[0]) if args else None)
        if fmt is None:
            nonliteral += 1
            fmt = '<non-literal>'
        out.append('line|-|%s' % canon_fmt(fmt, printf=bool(m.group('cfgline'))))
    for m in CALL.finditer(src):
        name = m.group('cfg') or m.group('lu')
        # find the matching close paren
        i, depth, instr = m.end(), 1, False
        while i < len(src) and depth:
            c = src[i]
            if instr:
                if c == '\\':
                    i += 2; continue
                if c == '"':
                    instr = False
            elif c == '"':
                instr = True
            elif c == '(':
                depth += 1
            elif c == ')':
                depth -= 1
            i += 1
        args = split_top(src[m.end():i - 1])
        chan = literal(args[0]) if args else None
        fmt = literal(args[1]) if len(args) > 1 else None
        if fmt is None:
            nonliteral += 1
            fmt = '<non-literal>'
        if chan is None:
            chan = '<non-literal>'
        out.append('%s|%s|%s' % (LEVEL[name], chan, canon_fmt(fmt, printf=bool(m.group('cfg')))))
    return out, nonliteral


SELFTEST = r'''
  cfg_logf("cd", "sector %u -> %08X", n, dst);
  lucent::debug("cd", "sector {} -> {:08X}", n, dst);
  cfg_logi("boot", "loaded %s (%d bytes)", p, sz);
  lucent::info("boot", "loaded {} ({} bytes)", p, sz);
  cfg_logw("gpu", "%-12s %.2f%%", name, pct);
  lucent::warn("gpu", "{:<12} {:.2f}%", name, pct);
  cfg_loge("cd", fmtvar, x);
  cfg_logi("padtr", "f%-4u bf839=%02X", f, b);
  lucent::info("padtr", "f{:<4} bf839={:02X}", f, b);
  cfg_line_addf(&l, " %02X", b);
  row.add(" {:02X}", b);
  cfg_logi("sbs", "delta %+d", d);
  lucent::info("sbs", "delta {:+}", d);
  lucent::info("renderdiff", "{:.2f}% pixels differ", pct);
  cfg_logi("renderdiff", "%.2f%% pixels differ", pct);
  cfg_logi("repl", "-> %s/p%%04d.ppm", dir);
  lucent::info("repl", "-> {}/p%04d.ppm", dir);
  cfg_logi("oraclediff", "wrote fb_{native,oracle}.ppm");
  lucent::info("oraclediff", "wrote fb_{{native,oracle}}.ppm");
'''


def selftest():
    import tempfile, os
    fd, path = tempfile.mkstemp(suffix='.cpp')
    os.write(fd, SELFTEST.encode()); os.close(fd)
    got, nonlit = sigs(path)
    os.unlink(path)
    ok = True
    # The whole point: each cfg_ line and its lucent:: twin must canonicalise IDENTICALLY.
    # (a, b) pairs that MUST canonicalise identically. Indices 0/1 are the two line
    # pieces, which sort ahead of everything else because they are emitted first.
    pairs = [(0, 1), (2, 3), (4, 5), (6, 7), (9, 10), (11, 12), (13, 14), (15, 16),
             (17, 18)]
    for a, b in pairs:
        if got[a] != got[b]:
            print('SELFTEST FAIL: %r != %r' % (got[a], got[b])); ok = False
    if nonlit != 1:
        print('SELFTEST FAIL: expected 1 non-literal format, saw %d' % nonlit); ok = False
    if len(got) != 19:
        print('SELFTEST FAIL: expected 19 sites, saw %d' % len(got)); ok = False
    # And it must be able to say NO: a changed template must not compare equal.
    if got[2] == got[4]:
        print('SELFTEST FAIL: two different templates compared equal'); ok = False
    print('\n'.join('  %s' % g for g in got))
    print('SELFTEST %s (%d sites, %d non-literal)' % ('PASS' if ok else 'FAIL', len(got), nonlit))
    return 0 if ok else 1


def main(argv):
    if len(argv) < 2:
        print(__doc__); return 2
    if argv[1] == '--selftest':
        return selftest()
    total, nonlit, lines = 0, 0, []
    for p in argv[1:]:
        s, nl = sigs(p)
        total += len(s); nonlit += nl
        lines.extend(s)
    if total == 0:
        # REFUSE to look like a clean result when nothing was scanned.
        sys.stderr.write('logsig: scanned %d file(s) and found ZERO call sites — that is a broken '
                         'scan, not an empty one\n' % (len(argv) - 1))
        return 3
    for l in sorted(lines):
        print(l)
    sys.stderr.write('logsig: %d call site(s) across %d file(s); %d had a non-literal format\n'
                     % (total, len(argv) - 1, nonlit))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
