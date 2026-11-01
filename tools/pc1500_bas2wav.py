#!/usr/bin/env python3
"""pc1500_bas2wav.py - BASIC text file -> tape recording (WAV) for CLOAD on a Sharp PC-1500(A).

Usage:  python3 pc1500_bas2wav.py program.txt out.wav [TAPENAME]
        python3 pc1500_bas2wav.py --check in.wav          (decode a recording, list its lines)

The text file has one numbered BASIC line per row, in the style of this repository
(upper case, keywords separated by spaces).  Format and token table were reverse-engineered
from the recordings in these repositories (wav/*.wav, Bastions of Retrocomputing); the
tokenizer reproduces 482 of 483 lines of seven recorded programs byte for byte (only TEXT and
CSIZE, line 380 of TEST7.3, are missing from the table).  Not verified on a real PC-1500 for
programs other than those.

Tape format (44100 Hz, 8 bit mono):
  bit 1 = 8 cycles of 2500 Hz, bit 0 = 4 cycles of 1250 Hz, 141 samples per bit.
  leader: 8 s of 2500 Hz.
  frame (11 bits) = one nibble: 0, d0 d1 d2 d3 (LSB first), 1 1 1 1 1 1.  A byte is sent as
  two frames, low nibble first.
  header block: nibble &A, then 42 bytes:
      10 11 12 13 14 15 16 17 01, name (26 bytes, padded with 0), C5, LEN hi, LEN lo
      (= bytes of the program before the final FF), 00 00, checksum hi, lo (sum of the 40 bytes before)
  gap of 76 one-bits, data block, gap of 73 one-bits, nibbles 5 5, 71 one-bits.
  data block: the program (per line: line number hi, lo, length, tokens, 0D; then FF) in
  chunks of 80 bytes, each chunk followed by its 16-bit sum (hi, lo); last chunk shorter.
"""
import re, sys, wave

TOK = {'AND':'f150','ASC':'f160','BEEP':'f182','CALL':'f18a','CHR$':'f163','CLEAR':'f187','DIM':'f18b',
 'END':'f18e','FOR':'f1a5','GOSUB':'f194','GOTO':'f192','IF':'f196','INKEY$':'f15c','INPUT':'f091',
 'INT':'f171','LEN':'f164','LET':'f198','LPRINT':'f0b9','MID$':'f17b','NEXT':'f19a','ON':'f19c',
 'OR':'f151','PEEK':'f16f','PEEK#':'f16e','POKE':'f1a1','POKE#':'f1a0','PRINT':'f097','RANDOM':'f1a8',
 'REM':'f1ab','RETURN':'f199','RND':'f17c','STR$':'f161','THEN':'f1ae','TIME':'f15b','TO':'f1b1',
 'VAL':'f162','WAIT':'f1b3','TEXT':'e686','CSIZE':'e680'}
FS = 44100
BIT = 141.12   # samples per bit (8 cycles of 2500 Hz)

def tokenize_line(no, text):
    out = bytearray(); i = 0; n = len(text)
    while i < n:
        c = text[i]
        if c == ' ':
            i += 1; continue
        if c == '"':
            j = text.find('"', i + 1); j = n - 1 if j < 0 else j
            out += text[i:j+1].encode('latin1'); i = j + 1; continue
        m = re.match(r'[A-Z]+[$#]?', text[i:])
        if m and m.group(0) in TOK:
            w = m.group(0); out += bytes.fromhex(TOK[w]); i += len(w)
            if w == 'REM':
                rest = text[i:]
                if rest.startswith(' '): rest = rest[1:]
                out += rest.encode('latin1'); i = n
            continue
        out.append(ord(c)); i += 1
    out.append(13)
    if len(out) > 255: raise ValueError('line %d too long' % no)
    return bytes([no >> 8, no & 255, len(out)]) + bytes(out)

def program_bytes(path):
    lines = []
    for l in open(path, encoding='latin1').read().splitlines():
        m = re.match(r'\s*(\d+)\s?(.*)$', l)
        if m: lines.append((int(m.group(1)), m.group(2).rstrip()))
    nums = [n for n, _ in lines]
    if nums != sorted(set(nums)): raise ValueError('line numbers must be ascending')
    return b''.join(tokenize_line(n, t) for n, t in lines) + b'\xff'

def header(name, prog):
    nm = name.upper().encode('latin1')[:7]
    h = bytes([0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x01]) + nm + bytes(26 - len(nm))
    n = len(prog) - 1
    h += bytes([0xC5, n >> 8, n & 255, 0, 0])
    s = sum(h)
    return h + bytes([s >> 8, s & 255])

def chunks(prog):
    out = bytearray()
    for p in range(0, len(prog), 80):
        c = prog[p:p+80]; s = sum(c)
        out += c + bytes([s >> 8, s & 255])
    return bytes(out)

def nibbles(bs):
    r = []
    for b in bs: r += [b & 15, b >> 4]
    return r

def bitstream(name, prog):
    bits = []
    def frames(nibs):
        for x in nibs: bits.extend([0] + [(x >> k) & 1 for k in range(4)] + [1]*6)
    frames([0xA] + nibbles(header(name, prog)))
    bits.extend([1]*76)
    frames(nibbles(chunks(prog)))
    bits.extend([1]*73)
    frames([5, 5])
    bits.extend([1]*71)
    return bits

def synth(bits, leader_s=8.0):
    # list of (half-cycle length in samples, level)
    edges = []
    t = 0.0
    def tone(f, cycles):
        nonlocal t
        half = FS / f / 2
        for _ in range(cycles):
            t += half; edges.append((t, 1))
            t += half; edges.append((t, 0))
    tone(2500, int(round(leader_s * 2500)))
    for b in bits:
        if b: tone(2500, 8)
        else: tone(1250, 4)
    total = int(t) + 1
    buf = bytearray([128]) * total
    lo, hi = 30, 226
    prev = 0.0
    k = 0
    # first half of each cycle is high: edges alternate (end of high, end of low)
    start = 0.0
    level_hi = True
    pos = 0.0
    for e, _ in edges:
        a, b = int(round(pos)), int(round(e))
        v = hi if level_hi else lo
        for i in range(a, min(b, total)): buf[i] = v
        pos = e; level_hi = not level_hi
    return bytes(buf)

def write_wav(path, data):
    w = wave.open(path, 'wb'); w.setnchannels(1); w.setsampwidth(1); w.setframerate(FS)
    w.writeframes(data); w.close()

def decode_wav(path):
    """returns (header bytes, program bytes with chunk sums verified)"""
    w = wave.open(path); d = w.readframes(w.getnframes())
    cr = []; prev = d[0] >= 128
    for i, v in enumerate(d):
        s = v >= 128
        if s and not prev: cr.append(i)
        prev = s
    runs = []
    for p in [b - a for a, b in zip(cr, cr[1:])]:
        c = 'S' if p >= 30 else ('F' if p < 22 else 'T')
        if c == 'T': continue
        if runs and runs[-1][0] == c: runs[-1][1] += p
        else: runs.append([c, p])
    r = []
    for c, t in runs:
        r += [0 if c == 'S' else 1] * round((t + (35 if c == 'S' else 17.6)) / 140.0)
    i = 0
    while i < len(r) and r[i] == 1: i += 1
    segs = []; cur = []
    while i + 11 <= len(r):
        f = r[i:i+11]
        if f[0] == 0 and f[5:] == [1]*6:
            cur.append(sum(x << k for k, x in enumerate(f[1:5]))); i += 11
        else:
            if cur: segs.append(cur); cur = []
            i += 1
    if cur: segs.append(cur)
    hdr = bytes((segs[0][k+1] << 4) | segs[0][k] for k in range(1, len(segs[0]) - 1, 2))
    raw = bytes((segs[1][k+1] << 4) | segs[1][k] for k in range(0, len(segs[1]) - 1, 2))
    prog = bytearray(); p = 0
    while p < len(raw):
        end = min(p + 80, len(raw) - 2); c = raw[p:end]
        if sum(c) & 0xffff != raw[end] * 256 + raw[end+1]: raise ValueError('chunk sum error at %d' % p)
        prog += c; p = end + 2
    return hdr, bytes(prog)

def list_program(prog):
    inv = {bytes.fromhex(v): k for k, v in TOK.items()}
    p = 0
    while prog[p] != 0xff:
        no = prog[p] * 256 + prog[p+1]; ln = prog[p+2]; body = prog[p+3:p+3+ln]; p += 3 + ln
        s = ''; i = 0
        while i < len(body) - 1:
            if body[i] in (0xf0, 0xf1, 0xe6) and body[i:i+2] in inv: s += ' ' + inv[body[i:i+2]] + ' '; i += 2
            else: s += chr(body[i]); i += 1
        print(no, s)

if __name__ == '__main__':
    a = sys.argv[1:]
    if len(a) == 2 and a[0] == '--check':
        h, p = decode_wav(a[1]); print('name:', h[9:35].rstrip(b'\0').decode('latin1'), 'bytes:', len(p)); list_program(p)
    elif len(a) in (2, 3):
        prog = program_bytes(a[0])
        name = a[2] if len(a) == 3 else re.sub(r'[^A-Za-z0-9]', '', a[0].split('/')[-1].split('.')[0])[:7]
        write_wav(a[1], synth(bitstream(name, prog)))
        print('%s: %d program bytes, tape name %s' % (a[1], len(prog), name.upper()))
    else:
        print(__doc__)
