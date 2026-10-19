/* DW1 USA mw_lib41 event/voice routines, translated from
 * asm/us/main/psyq/libsnd.s. Included after libsnd_seq.inc. */

/* asm: asm/us/main/psyq/libsnd.s: SsUtGetVagAtr / SsUtSetVagAtr.
 * Internal callers only change selected fields, so access the writable VH
 * directly. Reserved bytes 14/15 and 24..31 are never overwritten. */
static unsigned char *snd_tone(short vab, short program, unsigned tone)
{
    if (vm_setup(vab, program) < 0 || tone >= 16 ||
        (unsigned)current.compact >= vh16(current_bank->header + 0x12)) return NULL;
    return current_bank->tones + (current.compact * 16 + tone) * 32;
}

/* asm: asm/us/main/psyq/libsnd.s: _SsVmAlloc. Free silent voices win;
 * otherwise steal by priority, envelope, then greatest age. */
static int vm_choose_voice(void)
{
    unsigned priority = (unsigned)(int)(signed char)current.priority;
    unsigned envelope = 0xffff, age = 0;
    int chosen = vm_max;
    for (int i = 0; i < vm_max; i++) {
        SndVoice *v = voices + i;
        if (vm_reserved & (1u << i)) continue;
        if (!v->status && !v->env) { chosen = i; break; }
        if (v->priority < (uint16_t)priority) {
            priority = (uint16_t)v->priority;
            envelope = (uint16_t)v->env; age = (uint16_t)v->age;
            chosen = i;
        } else if (v->priority == (uint16_t)priority) {
            if ((uint16_t)v->env < envelope ||
                ((uint16_t)v->env == envelope && (int)age < v->age)) {
                envelope = (uint16_t)v->env; age = (uint16_t)v->age; chosen = i;
            }
        }
    }
    if (chosen < vm_max) {
        for (int i = 0; i < vm_max; i++)
            if (!(vm_reserved & (1u << i))) voices[i].age = (uint16_t)voices[i].age + 1;
        voices[chosen].age = 0;
        voices[chosen].auto_pan = voices[chosen].auto_vol = 0;
        voices[chosen].priority = (signed char)current.priority;
    }
    return chosen;
}

/* asm: asm/us/main/psyq/libsnd.s: note2pitch. This routine uses signed
 * center/shift and clamps the fraction, unlike note2pitch2. */
static uint16_t vm_seq_pitch(void)
{
    short note = (signed char)current.note + 60 - (signed char)current.center;
    int fraction = (signed char)current.shift / 8;
    fraction = (uint32_t)fraction >= 16 ? 15 : fraction;
    int octave = note / 12, index = (note - octave * 12) * 16 + fraction;
    if ((unsigned)index >= sizeof(snd_pitch_table) / sizeof(*snd_pitch_table)) return 0;
    uint32_t pitch = snd_pitch_table[index];
    octave = (short)(octave - 5);
    if (octave > 0) pitch <<= octave & 31;
    else if (octave < 0) pitch >>= (-octave) & 31;
    return pitch;
}

/* asm: asm/us/main/psyq/libsnd.s: _SsVmKeyOff. Noise notes clear the
 * software voice only; ordinary samples also queue KOFF. */
static void vm_note_off(short id, short vab, short program, short note)
{
    for (int i = 0; i < vm_max; i++) {
        SndVoice *v = voices + i;
        if ((vm_reserved & (1u << i)) || v->seq != id || v->vab != vab ||
            v->program != program || v->note != note) continue;
        if (v->vag == 0xff) v->status = v->vag = v->pitch = 0;
        else vm_key_off(i);
    }
}

/* asm: asm/us/main/psyq/libsnd.s: _SsVmKeyOn,
 * _SsVmSelectToneAndVag. All selected tones receive an allocated voice. */
static void vm_note_on(short id, short vab, short program, unsigned note,
                       unsigned velocity, unsigned pan)
{
    if (vm_setup(vab, program) < 0) return;
    SndSeq *q = scores[id & 255] + ((unsigned short)id >> 8);
    unsigned channel = q->state[0x17 - 0x14];
    unsigned char *p = current_bank->programs + program * 16;
    current.seq = id; current.note = note; current.fine = 0;
    current.volume = velocity * q->channel_volume[channel] / 127;
    current.pan = pan; current.program_vol = p[1]; current.program_pan = p[4];
    current.tones = p[0];
    if (current.compact >= vh16(current_bank->header + 0x12)) return;
    for (int tone = 0; tone < (signed char)current.tones && tone < 16; tone++) {
        unsigned char *t = current_bank->tones + (current.compact * 16 + tone) * 32;
        if ((signed char)current.note < t[6] || (signed char)current.note > t[7]) continue;
        current.tone = tone; current.vag = t[22];
        current.priority = t[0]; current.mode = t[1]; current.tone_vol = t[2];
        current.tone_pan = t[3]; current.center = t[4]; current.shift = t[5];
        current.min_note = t[6]; current.max_note = t[7];
        int voice = vm_choose_voice();
        if (voice >= vm_max) continue;
        current.voice = voice;
        SndVoice *v = voices + voice;
        v->status = 1; v->age = 0; v->seq = id; v->vab = (signed char)current.vab;
        v->compact = (signed char)current.compact; v->program = program;
        v->volume = velocity; v->channel_volume = channel; v->pan = pan;
        v->tone = tone; v->note = note; v->priority = (signed char)current.priority;
        v->vag = current.vag;
        vm_allocate();
        vm_key_volume(current.vag == 0xff, current.vag == 0xff ? 0 : vm_seq_pitch());
    }
}

/* asm: asm/us/main/psyq/libsnd.s: _SsVmPitchBend / _SsVmPBVoice.
 * The SEQ event ignores the LSB; positive/negative ranges divide by 63/64. */
static void vm_pitch_bend(short id, short vab, short program, unsigned value)
{
    if (vm_setup(vab, program) < 0) return;
    current.seq = id;
    for (int i = 0; i < vm_max; i++) {
        SndVoice *v = voices + i;
        if (v->seq != id || v->vab != vab || v->program != program) continue;
        unsigned char *t = snd_tone(vab, program, (uint16_t)v->tone);
        if (!t) continue;
        int bend = (short)(value - 64), note = (uint16_t)v->note, fine = 0;
        if (bend > 0) { int product = bend * t[13]; note += product / 63; fine = (product % 63) * 2; }
        else if (bend < 0) { int product = bend * t[12]; note += product / 64 - 1; fine = (product % 64) * 2 + 127; }
        current.voice = i; current.tone = v->tone;
        current.tone_reg = (signed char)current.compact * 16 + current.tone;
        vm_regs[i][2] = vm_note_pitch(note, fine); vm_dirty[i] |= 4;
    }
}

/* asm: asm/us/main/psyq/libsnd.s: _SsVmSetVol. Unlike SetSeqVol, this
 * path keeps full-width intermediates and does not update the voice pan. */
static void vm_channel_volume(short id, short vab, short program, unsigned volume, unsigned pan)
{
    if (vm_setup(vab, program) < 0) return;
    current.seq = id;
    SndSeq *q = scores[id & 255] + ((unsigned short)id >> 8);
    volume = (uint16_t)volume; pan = (uint16_t)pan;
    if (!volume) volume = 1;
    if (!pan) pan = 1;
    for (int i = 0; i < vm_max; i++) {
        SndVoice *v = voices + i;
        if ((vm_reserved & (1u << i)) || v->seq != id || v->vab != vab || v->program != program) continue;
        unsigned channel = q->state[0x17 - 0x14];
        if ((unsigned)(int)q->channel_volume[channel] != volume && !q->channel_volume[channel]) q->channel_volume[channel] = 1;
        unsigned char *t = current_bank->tones + (v->compact * 16 + v->tone) * 32;
        unsigned char *p = current_bank->programs + program * 16;
        int level = (int32_t)((uint32_t)(int32_t)v->volume * volume) / 127;
        level = (int32_t)((uint32_t)level * current_bank->header[24] * 16383u) / 16129;
        uint32_t gain = ((uint32_t)level * p[1] * t[2]) / 16129;
        uint32_t l = gain * (uint16_t)q->volume[0] / 127, r = gain * (uint16_t)q->volume[1] / 127;
        vm_pan(&l, &r, t[3], 63); vm_pan(&l, &r, p[4], 63); vm_pan(&l, &r, pan & 255, 63);
        if (vm_mono == 1) l = r = l > r ? l : r;
        vm_regs[i][0] = (l * l) / 16383; vm_regs[i][1] = (r * r) / 16383;
        vm_dirty[i] |= 3;
    }
}

/* asm: asm/us/main/psyq/libsnd.s: SsUtSetReverb{Type,Depth,Delay,Feedback},
 * SsUtReverbOn/Off. The shared _svm_rattr's inactive fields survive. */
static void snd_reverb_parameter(unsigned parameter, unsigned value)
{
    if (parameter == 15) {
        if (value >= 10) return;
        snd_reverb.mask = 1; snd_reverb.mode = value;
        if (!value) SpuSetReverb(0);
    } else if (parameter == 16) {
        snd_reverb.mask = 6;
        snd_reverb.depth.left = snd_reverb.depth.right = (short)(value * 32767 / 127);
    } else if (parameter == 17) { snd_reverb.mask = 16; snd_reverb.feedback = value; }
    else { snd_reverb.mask = 8; snd_reverb.delay = value; }
    SpuSetReverbModeParam(&snd_reverb);
}

/* asm: asm/us/main/psyq/libsnd.s: _SsSetNrpnVabAttr0..19,
 * _SsUtResolveADSR / _SsUtBuildADSR. For attrs 11..14 retail omits
 * ResolveADSR and reads uninitialized stack words. Preserve existing ADSR
 * fields instead; this defensive substitution is documented in libsnd.md. */
static void snd_nrpn_attribute(short vab, short program, unsigned tone, unsigned attribute, unsigned value)
{
    if (attribute >= 20) return;
    if (attribute >= 15) { snd_reverb_parameter(attribute, value); return; }
    unsigned char *t = snd_tone(vab, program, tone);
    if (!t) return;
    uint16_t a = vh16(t + 16), b = vh16(t + 18);
    switch (attribute) {
    case 0: t[0] = value; break;
    case 1: t[1] = value; if (value == 0 || value == 4) SpuSetReverb(value == 4); break;
    case 2: t[6] = value; break;
    case 3: t[7] = value; break;
    case 4: case 5: a = (a & 0xff) | ((value & 127) << 8) | (attribute == 5 ? 0x8000 : 0); break;
    case 6: a = (a & ~0xf0) | ((value & 15) << 4); break;
    case 7: a = (a & ~15) | (value & 15); break;
    case 8: case 9: b = (b & ~0x9fc0) | ((value & 127) << 6) | (attribute == 9 ? 0x8000 : 0); break;
    case 10: case 11: b = (b & ~63) | (value & 31) | (attribute == 11 ? 32 : 0); break;
    case 12:
        if (value >= 1 && value < 64) b &= ~0x4000;
        else if (value >= 64 && value < 128) b |= 0x4000;
        break;
    case 13: t[9] = value; break;
    case 14: t[10] = value; break;
    }
    /* BuildADSR also clears the unused 13th bit in ADSR2. */
    if (attribute >= 4) { vh_put16(t + 16, a); vh_put16(t + 18, b & ~0x2000); }
}

/* asm: asm/us/main/psyq/libsnd.s: _SsContDataEntry. RPN fine/coarse
 * tuning writes its existing byte back in DW1; only RPN 0 changes PB range. */
static void snd_data_entry(SndSeq *q, unsigned value)
{
    unsigned char *s = q->state;
    short vab = (signed char)s[0x26 - 0x14], program = s[0x37 - 0x14 + s[3]];
    /* SsUtGetProgAtr runs even when this data entry supplies a loop count. */
    int setup = vm_setup(vab, program);
    unsigned tones = setup < 0 ? 0 : current_bank->programs[program * 16];
    if (s[0x1c - 0x14] == 1 && !s[1]) {
        s[0x1d - 0x14] = value; s[0x1c - 0x14] = 0; s[1] = 1;
    } else if (s[0x1e - 0x14] == 2) {
        if (!s[0x19 - 0x14] && setup == 0) {
            for (unsigned tone = 0; tone < tones && tone < 16; tone++) {
                unsigned char *t = snd_tone(vab, program, tone);
                if (t && !s[0x18 - 0x14]) t[12] = t[13] = value & 127;
            }
        }
        s[0x1e - 0x14] = 0;
    } else if (s[0x1f - 0x14] == 2) {
        if (s[0x1b - 0x14] == 16 && setup == 0) {
            for (unsigned tone = 0; tone < tones && tone < 16; tone++)
                snd_nrpn_attribute(vab, program, tone, s[0x1a - 0x14], value);
        } else snd_nrpn_attribute(vab, program, s[0x1b - 0x14], s[0x1a - 0x14], value);
        s[0x1f - 0x14] = 0;
    }
    q->initial_delta = snd_delta(q);
}

/* asm: asm/us/main/psyq/libsnd.s: _SsSetControlChange, _SsContBankChange,
 * MainVol/Panpot/Expression/Damper/External/Nrpn1/Nrpn2/Rpn1/Rpn2/ResetAll. */
static void snd_control(short id, SndSeq *q, unsigned control)
{
    unsigned value = *q->cursor++;
    unsigned char *s = q->state;
    unsigned channel = s[3];
    short vab = (signed char)s[0x26 - 0x14], program = s[0x37 - 0x14 + channel];
    switch (control) {
    case 0: s[0x26 - 0x14] = value; break;
    case 6: snd_data_entry(q, value); return;
    case 7:
        vm_channel_volume(id, vab, program, value, s[0x27 - 0x14 + channel]);
        q->channel_volume[channel] = value; break;
    case 10:
        vm_channel_volume(id, vab, program, q->channel_volume[channel], value);
        s[0x27 - 0x14 + channel] = value; break;
    case 11:
        if (vm_setup(vab, program) == 0) current_bank->programs[program * 16 + 1] = value;
        vm_channel_volume(id, vab, program, q->channel_volume[channel], s[0x27 - 0x14 + channel]); break;
    case 64: vm_damper = value >= 64 ? 2 : 0; break;
    case 91: snd_reverb_parameter(16, value); break;
    case 98:
        if (s[0x1c - 0x14] == 1 && !s[1]) {
            s[0x1d - 0x14] = value; s[0x1c - 0x14] = 0; s[1] = 1;
        } else if (s[0x1b - 0x14] != 20 && s[0x1b - 0x14] != 30) {
            s[0x1a - 0x14] = value; s[0x1c - 0x14] = 0; s[0x1f - 0x14]++;
        }
        /* _SsMarkCallback is NULL: no reached API installs a marker callback. */
        break;
    case 99:
        s[0x1b - 0x14] = value;
        if (value == 20) {
            s[0x1c - 0x14] = 1;
            q->initial_delta = snd_delta(q); q->loop = q->cursor; return;
        }
        if (value == 30) {
            q->initial_delta = snd_delta(q);
            if (!s[0x1d - 0x14]) s[1] = 0;
            else if (s[0x1d - 0x14] < 127) {
                if (--s[0x1d - 0x14]) q->cursor = q->loop;
                else s[1] = 0;
            } else { q->initial_delta = 0; q->cursor = q->loop; }
            return;
        }
        s[0x1f - 0x14]++; break;
    case 100: s[0x18 - 0x14] = value; s[0x1e - 0x14]++; break;
    case 101: s[0x19 - 0x14] = value; s[0x1e - 0x14]++; break;
    case 121:
        SpuSetReverb(0); vm_damper = 0;
        s[0x37 - 0x14 + channel] = channel;
        s[0x18 - 0x14] = s[0x19 - 0x14] = 0;
        q->channel_volume[channel] = 127; s[0x27 - 0x14 + channel] = 64; break;
    }
    q->initial_delta = snd_delta(q);
}

/* asm: asm/us/main/psyq/libsnd.s: _SsSeqGetEof / _SsSndNextSep. */
static void snd_eof(short id, SndSeq *q)
{
    unsigned char *s = q->state;
    s[0x21 - 0x14]++;
    unsigned char *restart = q->flags & 0x400 ? q->next : q->start;
    if (!s[0x20 - 0x14] || (signed char)s[0x21 - 0x14] < (signed char)s[0x20 - 0x14]) {
        q->elapsed = 0; s[0x1c - 0x14] = 0; q->initial_delta = 0;
        q->cursor = restart;
        if (s[0x20 - 0x14]) q->loop = restart;
    } else {
        q->flags = (q->flags & ~(1 | 8 | 2)) | 0x204;
        s[0] = 0; q->loop = restart;
        int next = (signed char)s[0x22 - 0x14], sep = s[0x23 - 0x14];
        if (next != -1) {
            if ((unsigned)next < (unsigned)seq_count && sep < sep_count && scores[next]) {
                SndSeq *n = scores[next] + sep;
                n->state[0x20 - 0x14] = 1; n->state[0x21 - 0x14] = 0;
                n->flags = (n->flags & ~(0x100 | 8 | 2 | 4 | 0x200)) | 1;
                n->state[0] = 1; n->cursor = n->start;
            }
            vm_seq_key_off(id);
        }
        vm_seq_key_off(id);
        q->initial_delta = q->interval;
    }
}

/* asm: asm/us/main/psyq/libsnd.s: _SsGetMetaEvent. DW1 consumes three
 * tempo bytes immediately after the event type, without an SMF length. */
static void snd_meta(SndSeq *q)
{
    uint32_t us = (q->cursor[0] << 16) | (q->cursor[1] << 8) | q->cursor[2];
    q->cursor += 3;
    if (us && q->resolution && tick.rate) {
        q->initial_tempo = 60000000 / us;
        uint32_t product = (uint32_t)(int32_t)q->resolution * q->initial_tempo;
        uint32_t numerator = product * 10, denominator = (uint32_t)tick.rate * 60;
        if (product && denominator) {
            if (numerator < denominator) q->repeat = q->interval = (uint32_t)tick.rate * 600 / product;
            else {
                q->repeat = -1;
                q->interval = numerator / denominator + (numerator % denominator > (uint32_t)tick.rate * 30);
            }
        }
    }
    q->initial_delta = snd_delta(q);
}

/* asm: asm/us/main/psyq/libsnd.s: _SsGetSeqData, _SsNoteOn,
 * _SsSetProgramChange / _SsSetPitchBend. The 0x80 status is not dispatched
 * by the retail jump table: note-off uses a 0x90 event with velocity zero. */
static void snd_event(short id, SndSeq *q)
{
    unsigned byte = *q->cursor++;
    if ((q->flags & 0x401) == 0x401 && q->saved && q->cursor == q->saved + 1) {
        snd_eof(id, q); return;
    }
    unsigned char *s = q->state;
    unsigned status = s[2];
    if (byte & 128) {
        s[3] = byte & 15;
        status = byte & 0xf0;
        if (status == 0xf0) status = 0xff;
        if (status != 0x90 && status != 0xb0 && status != 0xc0 && status != 0xe0 && status != 0xff) return;
        s[2] = status; byte = *q->cursor++;
    }
    unsigned channel = s[3];
    short vab = (signed char)s[0x26 - 0x14], program = s[0x37 - 0x14 + channel];
    switch (status) {
    case 0x90: {
        unsigned velocity = *q->cursor++;
        q->initial_delta = snd_delta(q);
        if (!velocity) vm_note_off(id, vab, program, byte);
        else if (!((uint16_t)q->play_count & (1u << channel)))
            vm_note_on(id, vab, program, byte, velocity, s[0x27 - 0x14 + channel]);
        break;
    }
    case 0xb0: snd_control(id, q, byte); break;
    case 0xc0: s[0x37 - 0x14 + channel] = byte; q->initial_delta = snd_delta(q); break;
    case 0xe0: vm_pitch_bend(id, vab, program, *q->cursor++); q->initial_delta = snd_delta(q); break;
    case 0xff: if (byte == 0x2f) snd_eof(id, q); else snd_meta(q); break;
    }
}

/* asm: asm/us/main/psyq/libsnd.s: _SsSndPlay -> _SsSeqPlay.
 * Flush precedes dispatch, so queued voice writes appear on the next tick. */
static void snd_play(short id, SndSeq *q)
{
    int32_t remaining = (uint32_t)q->initial_delta - (int32_t)q->interval;
    if (remaining > 0) {
        if (q->repeat > 0) q->repeat--;
        else if (!q->repeat) { q->repeat = q->interval; q->initial_delta = (uint32_t)q->initial_delta - 1; }
        else q->initial_delta = remaining;
    } else {
        int32_t elapsed = q->initial_delta;
        for (;;) {
            snd_event(id, q);
            if (!q->initial_delta) continue;
            elapsed = (uint32_t)elapsed + q->initial_delta;
            if (elapsed >= q->interval) break;
        }
        q->initial_delta = (uint32_t)elapsed - (int32_t)q->interval;
    }
}

/* asm: asm/us/main/psyq/libsnd.s: _SsSndCrescendo / _SsSndDecrescendo.
 * Both routines use the same 0x9c/0xa0 counters and 0x48..0x4c fields. */
static void snd_fade(short id, SndSeq *q, int down)
{
    int flag = down ? 0x20 : 0x10;
    q->decrescendo = (uint32_t)q->decrescendo - 1;
    if (q->decrescendo < 0) q->flags &= ~flag;
    else if (q->fade[2] && (q->fade[2] < 0 || q->decrescendo % q->fade[2] == 0)) {
        int period = q->fade[2];
        q->fade[1] = (uint16_t)q->fade[1] + (period > 0 ? -1 : period);
        if (q->fade[1] < 0) {
            vm_seq_volume(id, down ? 0 : 127, down ? 0 : 127); q->flags &= ~flag;
        } else {
            unsigned l = (uint16_t)q->volume[0], r = (uint16_t)q->volume[1];
            if (period > 0) {
                if (!down && (int)l + q->fade[1] >= (int)l + 1) vm_seq_volume(id, l + 1, r + 1);
                if (down && !((int)l - 1 < (int)l - q->fade[1] && (int)r - 1 < (int)r - q->fade[1])) {
                    if (!l || !r) q->flags &= ~flag;
                    else vm_seq_volume(id, l - 1, r - 1);
                }
            } else {
                if ((!down && (int)l - period >= 127 && (int)r - period >= 127) ||
                    (down && (int)l + period <= 0 && (int)r + period <= 0)) {
                    vm_seq_volume(id, down ? 0 : 127, down ? 0 : 127); q->flags &= ~flag;
                }
                int32_t progress = (uint32_t)((uint32_t)q->crescendo - q->decrescendo) * (unsigned)-period;
                if (q->fade[0] < progress) {
                    if (down && (!l || !r)) q->flags &= ~flag;
                    else vm_seq_volume(id, l + (down ? period : -period), r + (down ? period : -period));
                }
            }
        }
        if (!q->decrescendo || q->fade[1] <= 0) q->flags &= ~flag;
    }
    current.seq = id; /* _SsVmGetSeqVol also changes the shared context. */
    q->volume[2] = q->volume[0]; q->volume[3] = q->volume[1];
}

/* asm: asm/us/main/psyq/libsnd.s: _SsSndTempo. These counters live at
 * 0xa8/0xac, distinct from the initial/current BPM at 0x8c/0x94. */
static void snd_tempo_fade(SndSeq *q)
{
    q->next_seq = (uint32_t)q->next_seq - 1;
    if (q->next_seq < 0) { q->flags &= ~0xc0; return; }
    int step = q->fade[3];
    if (step > 0 && q->next_seq % step) return;
    uint32_t tempo = q->initial_tempo, target = q->next_sep;
    if (step > 0) { if (tempo > target) tempo--; else if (tempo < target) tempo++; }
    else if (tempo > target) { tempo += step; if (tempo < target) tempo = target; }
    else if (tempo < target) { tempo -= step; if (tempo > target) tempo = target; }
    q->initial_tempo = tempo;
    if (tick.rate) q->interval = ((uint32_t)(int32_t)q->resolution * tempo * 10) / ((uint32_t)tick.rate * 60);
    if (q->interval <= 0) q->interval = 1;
    if (!q->next_seq || tempo == target) q->flags &= ~0xc0;
}
