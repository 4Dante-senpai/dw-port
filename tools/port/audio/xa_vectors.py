#!/usr/bin/env python3
"""Independent scalar XA equations; synthetic bytes only. No decoder code imported.
Coefficients from psx-spx Table1..7. Output hashes are PCM16LE at 44100 Hz.
"""
import hashlib
from pathlib import Path
import struct
import sys

TABLES = [[0, 0, 0, 0, 0, -2, 10, -34, 65, -84, 52, 9, -266, 1024, -2680, 9036, 26516, -6016, 3021, -1571, 848, -365, 107, 10, -16, 17, -8, 3, -1], [0, 0, 0, -2, 0, 3, -19, 60, -75, 162, -227, 306, -67, -615, 3229, 29883, -4532, 2488, -1471, 882, -424, 166, -27, 5, 6, -8, 3, -1, 0], [0, 0, -1, 3, -2, -5, 31, -74, 179, -402, 689, -926, 1272, -1446, 31033, -1446, 1272, -926, 689, -402, 179, -74, 31, -5, -2, 3, -1, 0, 0], [0, -1, 3, -8, 6, 5, -27, 166, -424, 882, -1471, 2488, -4532, 29883, 3229, -615, -67, 306, -227, 162, -75, 60, -19, 3, 0, -2, 0, 0, 0], [-1, 3, -8, 17, -16, 10, 107, -365, 848, -1571, 3021, -6016, 26516, 9036, -2680, 1024, -266, 9, 52, -84, 65, -34, 10, -1, 0, 1, 0, 0, 0], [2, -8, 16, -35, 43, 26, -235, 635, -1352, 2810, -5882, 21472, 15367, -4681, 2062, -839, 347, -68, -23, 70, -35, 17, -5, 0, 0, 0, 0, 0, 0], [-5, 17, -35, 70, -23, -68, 347, -839, 2062, -4681, 15367, 21472, -5882, 2810, -1352, 635, -235, 26, 43, -35, 16, -8, 2, 0, 0, 0, 0, 0, 0]]

def vector(coding):
    stereo, half, wide = coding & 1, coding & 4, coding & 16
    history = [[0, 0], [0, 0]]
    samples = [[], []]
    pcm = bytearray()
    previous = [0, 0]
    for sector in range(2):
        for group in range(18):
            for unit in range(0, 4 if wide else 8, 2 if stereo else 1):
                block = [[], []]
                for channel in range(2 if stereo else 1):
                    u = unit + channel
                    shift = (group + u) % 16
                    # psx-spx: reserved shifts 13..15 behave as shift 9.
                    divisor = 512 if shift >= 13 else 2**shift
                    filt = (group + u) % 4
                    old, older = history[channel]
                    for j in range(28):
                        offset = j*4 + (u if wide else u//2)
                        byte = ((group*128+16+offset)*37 + sector*53 + 11) & 255
                        n = byte if wide else (byte >> (4*(u%2))) & 15
                        if n >= (128 if wide else 8): n -= 256 if wide else 16
                        raw = (n * (256 if wide else 4096)) // divisor
                        value = max(-32768, min(32767, raw + (old*[0,60,115,98][filt] + older*[0,0,-52,-55][filt] + 32)//64))
                        block[channel].append(value)
                        older, old = old, value
                    history[channel] = [old, older]
                if not stereo: block[1] = block[0]
                for pair in zip(*block):
                    feed = [tuple((previous[c]+pair[c])//2 for c in range(2)), pair] if half else [pair]
                    for frame in feed:
                        for c in range(2): samples[c].append(frame[c])
                        if len(samples[0]) % 6 == 0:
                            for table in TABLES:
                                out = []
                                for c in range(2):
                                    value = sum((samples[c][-1-i] if i<len(samples[c]) else 0)*weight//32768 for i,weight in enumerate(table))
                                    out.append(max(-32768,min(32767,value)))
                                pcm.extend(struct.pack('<hh', *out))
                    previous = pair
    return hashlib.sha256(pcm).hexdigest()

def content():
    return '/* Generated independently by tools/port/audio/xa_vectors.py. */\n' + 'static const char *const hashes[8] = {\n' + ''.join('    "'+vector(c)+'",\n' for c in (0,1,4,5,16,17,20,21)) + '};\n'

if __name__ == '__main__':
    path=Path(__file__).resolve().parents[3]/'port/backend/tests/xa_vectors.h'
    if '--check' in sys.argv:
        if path.read_text()!=content(): raise SystemExit('stale XA vectors')
    else: path.write_text(content())
