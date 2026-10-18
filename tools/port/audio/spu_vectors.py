#!/usr/bin/env python3
"""Independent L1 expectations from psx-spx SPU equations; no renderer imports.

Print frozen SHA-256 of signed little-endian samples. Gaussian coefficients here
are fixed hardware table entries at phases 0 and 128, not read from C.
Reverb uses an analytic read-only impulse through APF2=0 and the documented FIR;
ROOM parameters are tested separately with live writes and guarded RAM.
"""
import hashlib
import struct
import sys
from pathlib import Path

def digest(values):
    return hashlib.sha256(struct.pack('<' + 'h' * len(values), *values)).hexdigest()

def sat(v):
    return min(32767, max(-32768, v))

def adpcm():
    h = [0, 0]
    decoded = [0, 0, 0]
    for filt in range(5):
        for shift in range(13):
            for j in range(28):
                byte = ((j // 2) * 37 + 19) & 255
                nib = (byte >> (4 * (j % 2))) & 15
                raw = (nib if nib < 8 else nib - 16) * 4096
                prediction = (h[-1] * [0, 60, 115, 98, 122][filt]
                              + h[-2] * [0, 0, -52, -55, -60][filt] + 32) // 64
                h.append(sat((raw >> shift) + prediction))
                decoded.append(h[-1])
    pcm = []
    for i in range(65 * 28):
        sample = sum(c * decoded[i + j] // 32768
                     for j, c in enumerate([4807, 22963, 4871, -1]))
        sample = sample * 32767 // 32768
        sample = sample * 32766 // 32768
        sample = sample * 32766 // 32768
        pcm.extend([sat(sample)] * 2)
    return pcm

def pitched(step, impulse=False):
    # Decode a continuous stream independently, preserving predictor history at
    # block boundaries. Both selected pitches visit only Gaussian phases 0/128.
    decoded = [0, 0, 0]
    for block in range(6):
        filt, shift = ((0, 0) if impulse else (block % 2 * 2, 2 + block % 2))
        for j in range(28):
            byte = (0x07 if block == 0 and j // 2 == 0 else 0) if impulse else (j // 2 * 37 + 19 + block * 11) & 255
            nib = (byte >> (j % 2 * 4)) & 15
            prediction = (decoded[-1] * [0, 60, 115][filt]
                          + decoded[-2] * [0, 0, -52][filt] + 32) // 64
            decoded.append(sat(((nib if nib < 8 else nib - 16) * 4096 >> shift) + prediction))
    coefficients = {0: [4807, 22963, 4871, -1], 128: [412, 15855, 15948, 424]}
    values = []
    for counter in range(0, 84 * step, step):
        index, phase = counter // 4096, (counter // 16) % 256
        x = sum(c * decoded[index + j] // 32768 for j, c in enumerate(coefficients[phase]))
        values.append((x * 32767 // 32768) * 32766 // 32768)
    return values

def pitch_pcm(step):
    return [sat(x * 32766 // 32768) for x in pitched(step) for _ in range(2)]

# A constant filter-0 nibble 7 gives Gaussian output 28559 at integer phase.
# ENVX=18799 gives exactly 16384 before voice volume, including a muted voice.
# PMON therefore scales voice 1's 0x1000 step by (32768+16384)/32768 = 1.5.
assert 28559 * 18799 // 32768 == 16384

# Piecewise analytic attack rates at shift=9, +28 until 0x6000 then +7.
exp = [min(28 * k, 32767) if k <= 878 else min(24584 + 7 * (k - 878), 32767)
       for k in range(1, 1201)]
linear = list(range(7, 7 * 32 + 1, 7))
# Fast attack reaches full at sample 3; decay halves until sustain threshold 2048.
decay = [14336, 28672, 32767, 16383, 8191, 4095, 2047] + [2047] * 9
sustain = [14336, 28672, 32767, 16383] + [16383 - 8 * k for k in range(1, 29)]
release = [max(16383 - 8 * k, 0) for k in range(1, 2101)]
exprelease = []
v = 16383
for _ in range(128):
    v = max(0, v + (-8 * v // 32768))
    exprelease.append(v)

# A single 22.05 kHz impulse (10000) at sample 1; interpolation FIR gain x2,
# then EVOL=0x7fff. APF2 coefficient zero selects its delayed sample directly.
fir = [-1,0,2,0,-10,0,35,0,-103,0,266,0,-616,0,1332,0,-2960,0,
       10246,16384,10246,0,-2960,0,1332,0,-616,0,266,0,-103,0,35,0,-10,0,2,0,-1]
reverb = []
for i in range(64):
    x = sat(((fir[i - 1] * 10000) // 32768) * 2) if 1 <= i <= 39 else 0
    reverb.extend([x * 32767 // 32768] * 2)

def room(voice_input=None, enabled=True):
    # ROOM: documented register constants, fixed synthetic work area and CD impulse.
    regs = [0x7d,0x5b,0x6d80,0x54b8,0xbed0,0,0,0xba80,0x5800,0x5300,
            0x4d6,0x333,0x3f0,0x227,0x374,0x1ef,0x334,0x1b5,0,0,0,0,0,0,
            0,0,0x1b4,0x136,0xb8,0x5c,0x8000,0x8000]
    area = [((i * 97 + 123) % 65536) - 32768 if voice_input is None else 0 for i in range(0x26c0 // 2)]
    inputs = [[], []]
    outputs = [[], []]
    pcm = []
    cursor = 0
    def signed(v):
        return v if v < 32768 else v - 65536
    def gain(v, coefficient):
        return sat(v * signed(coefficient) // 32768)
    def convolution(values):
        return sum(a * b for a, b in zip(fir, reversed(values[-39:]))) // 32768
    for t in range(1024 if voice_input is None else 12000):
        dry = 0 if voice_input is None or t >= len(voice_input) else voice_input[t]
        for side in range(2):
            # CD volume 0x7fff; one impulse at t=0, with ATTR CD reverb enabled.
            inputs[side].append((12000 * 32767 // 32768 if t == 0 else 0)
                                if voice_input is None else (dry if enabled else 0))
            outputs[side].append(0)
        if t % 2:
            def read(index, displacement=0):
                return area[(cursor + regs[index] * 4 - displacement) % len(area)]
            def write(index, value):
                area[(cursor + regs[index] * 4) % len(area)] = sat(value)
            for dest, delay, side in [(10,16,0),(11,17,1),(18,25,0),(19,24,1)]:
                old = read(dest,1)
                v = gain(sat(convolution(inputs[side])), regs[30+side])
                write(dest, gain(sat(v + gain(read(delay),regs[7]) - old),regs[2]) + old)
            for side in range(2):
                v = sat(sum(gain(read(index+side),regs[j+3])
                            for j,index in enumerate([12,14,20,22])))
                for j in range(2):
                    dest = 26 + j*2 + side
                    delayed = read(dest,regs[j]*4)
                    v = sat(v - gain(delayed,regs[8+j]))
                    write(dest,v)
                    v = sat(gain(v,regs[8+j]) + delayed)
                outputs[side][-1] = v
            cursor = (cursor+1) % len(area)
        for side in range(2):
            pcm.append(sat(sat(convolution(outputs[side])*2) * 32767 // 32768
                           + dry * 32766 // 32768))
    return pcm

def noise():
    level = 1
    pcm = []
    for t in range(132):
        # Shift=15, step=4: the LFSR advances on every sample.
        feedback = ((level >> 15) ^ (level >> 12) ^ (level >> 11) ^ (level >> 10) ^ 1) & 1
        level = (level * 2 + feedback) & 65535
        if level >= 32768:
            level -= 65536
        if t >= 4:
            x = level * 32767 // 32768
            x = x * 32766 // 32768
            x = x * 32766 // 32768
            pcm.extend([x] * 2)
    return pcm

if __name__ == '__main__':
    for name, samples in [('adpcm', adpcm()), ('attack_linear', linear),
                          ('attack_exp', exp), ('decay', decay), ('sustain', sustain),
                          ('release', release), ('release_exp', exprelease), ('reverb', reverb), ('room', room()), ('noise', noise()),
                          ('fractional', pitch_pcm(0x1800)), ('pmon_off', pitch_pcm(0x1000)),
                          ('pmon', pitch_pcm(0x1800)),
                          ('eon_off', room(pitched(0x1000, True), False)),
                          ('eon_on', room(pitched(0x1000, True)))]:
        value = digest(samples)
        if '--check' in sys.argv:
            fixture = Path(__file__).resolve().parents[3] / 'port/backend/tests/test_spu.c'
            assert value in fixture.read_text(), (name, value)
        print(name, value)
