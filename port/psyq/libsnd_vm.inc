/* DW1 mw_lib41 voice manager. Included after the native VAB/SEQ state.
 * asm: asm/us/main/psyq/libsnd.s. No DW2-PC implementation is copied. */
#include "../backend/spu.h"

typedef struct {
    short vag, age, pitch, env, volume;
    unsigned char pan, reserved;
    short channel_volume, note, seq, compact, program, tone, vab, priority;
    unsigned char channel, status;
    short auto_vol, vol_delta, vol_period, vol_count, vol_current, vol_target;
    short auto_pan, pan_delta, pan_period, pan_count, pan_current, pan_target;
} SndVoice;
_Static_assert(sizeof(SndVoice) == 0x36 && offsetof(SndVoice, status) == 0x1d,
               "DW1 voice layout; DW2 uses 0x38");
typedef struct {
    unsigned char tones, vab, note, fine, volume, pan, program, compact;
    short velocity;
    unsigned char program_vol, program_pan, tone, tone_vol, tone_pan, priority;
    unsigned char center, shift, min_note, max_note, mode, reserved;
    short seq, vag, voice, voice_reg, tone_reg;
} SndCurrent;
_Static_assert(sizeof(SndCurrent) == 0x20, "DW1 current voice layout");
static SndVoice voices[24];
static SndCurrent current;
static uint16_t vm_regs[24][8];
static unsigned char vm_dirty[24];
static uint32_t vm_on, vm_off, vm_reverb, vm_noise, vm_reserved;
static uint32_t env_history[16];
static unsigned env_index;
static int vm_busy, vm_max, vm_auto_off, vm_mono;
static short vm_damper;
static uint32_t vm_alloc[66];
static SndBank *current_bank;
static void vm_auto_volume(int voice);
#include "libsnd_pitch.inc"

/* asm: asm/us/main/psyq/libsnd.s: _SsVmVSetUp. */
static int vm_setup(short vab, short program)
{
    if ((unsigned short)vab >= 16 || banks[vab].used != 1 || program < 0 || program >= max_programs)
        return -1;
    current.vab = vab; current.program = program;
    current_bank = banks + vab;
    current.compact = current_bank->programs[program * 16 + 8];
    return 0;
}

/* asm: asm/us/main/psyq/libsnd.s: _SsVmDoAllocate. DW1 stages the VAG
 * start, ADSR and ENVX history even for an explicitly selected voice. */
static void vm_allocate(void)
{
    int voice = current.voice;
    current.voice_reg = voice * 8;
    current.tone_reg = (signed char)current.compact * 16 + (signed char)current.tone;
    voices[voice].env = 0x7fff;
    for (int i = 0; i < 16; i++) env_history[i] &= ~(1u << voice);
    int vag = current.vag;
    unsigned char *p = current_bank->programs + ((vag - 1) / 2) * 16 + (vag & 1 ? 12 : 14);
    unsigned char *t = current_bank->tones + current.tone_reg * 32;
    vm_regs[voice][3] = vh16(p);
    vm_regs[voice][4] = vh16(t + 16);
    vm_regs[voice][5] = vh16(t + 18) + vm_damper;
    vm_dirty[voice] |= 0x38;
}

/* asm: asm/us/main/psyq/libsnd.s: note2pitch2. Division is signed and
 * truncates toward zero; the final variable shift uses MIPS's low five bits. */
static uint16_t vm_note_pitch(short note, short fine)
{
    unsigned char *t = current_bank->tones + current.tone_reg * 32;
    int fraction = ((unsigned short)fine + t[5]) / 8;
    int carry = fraction >= 16;
    if (carry) fraction -= 16;
    short n = note + 60 - t[4] + carry;
    int octave = n / 12;
    int index = (n - octave * 12) * 16 + (short)fraction;
    /* Invalid caller notes/fine values index outside the retail table. */
    if ((unsigned)index >= sizeof(snd_pitch_table) / sizeof(*snd_pitch_table)) return 0;
    uint32_t pitch = snd_pitch_table[index];
    octave -= 5;
    if (octave > 0) pitch <<= octave & 31;
    else if (octave < 0) pitch >>= (-octave) & 31;
    return pitch;
}

/* asm: asm/us/main/psyq/libsnd.s: _SsVmKeyOnNow / vmNoiseOn, fixed-point
 * gain common to both paths. The second quotient is an unsigned division. */
static uint32_t vm_gain(short volume)
{
    int32_t product = (uint32_t)(int32_t)volume * current_bank->header[0x18] * 0x3fff;
    int32_t level = product / 16129;
    uint32_t scaled = (uint32_t)level * (int)(signed char)current.program_vol * (int)(signed char)current.tone_vol;
    return scaled / 16129;
}

/* asm: asm/us/main/psyq/libsnd.s: pan stages in _SsVmKeyOnNow/vmNoiseOn.
 * The denominator is 63 here, and 64 in SetAutoVol. */
static void vm_pan(uint32_t *left, uint32_t *right, int pan, unsigned divisor)
{
    if ((uint32_t)pan < 64) *right = (*right * (uint32_t)pan) / divisor;
    else *left = (*left * (uint32_t)(127 - pan)) / divisor;
}

/* asm: asm/us/main/psyq/libsnd.s: _SsVmKeyOnNow / vmNoiseOn gain stages. */
static void vm_key_volume(int noise, uint16_t pitch)
{
    int voice = current.voice;
    uint32_t left = vm_gain((signed char)current.volume), right = left;
    if (current.seq != 0x21) {
        SndSeq *q = scores[current.seq & 255] + ((unsigned short)current.seq >> 8);
        left = (left * (unsigned short)q->volume[0]) / 127;
        right = (right * (unsigned short)q->volume[1]) / 127;
    }
    vm_pan(&left, &right, noise ? (signed char)current.tone_pan : current.tone_pan, 63);
    vm_pan(&left, &right, noise ? (signed char)current.program_pan : current.program_pan, 63);
    vm_pan(&left, &right, noise ? (signed char)current.pan : current.pan, 63);
    if (vm_mono) left = right = left < right ? right : left;
    if (current.seq != 0x21) { left = (left * left) / 16383; right = (right * right) / 16383; }
    vm_regs[voice][0] = left; vm_regs[voice][1] = right;
    if (!noise) {
        vm_regs[voice][2] = pitch;
        vm_dirty[voice] |= 7;
        voices[voice].pitch = pitch;
        vm_noise &= ~(1u << voice);
    } else {
        SpuSetNoiseClock(((signed char)current.note - (signed char)current.center) & 63);
        vm_dirty[voice] |= 3;
        voices[voice].pitch = 10;
        for (int i = 0; i < vm_max; i++) if (!(vm_reserved & (1u << i))) voices[i].status &= 1;
        voices[voice].status = 2;
        vm_noise = 1u << voice;
    }
    if (current.mode & 4) vm_reverb |= 1u << voice;
    else vm_reverb &= ~(1u << voice);
    vm_on |= 1u << voice;
    vm_off &= ~vm_on;
}

/* asm: asm/us/main/psyq/libsnd.s: _SsVmKeyOffNow (argument unused). */
static void vm_key_off(int voice)
{
    current.voice = voice;
    voices[voice].status = 0;
    voices[voice].pitch = voices[voice].vag = 0;
    vm_off |= 1u << voice;
    vm_on &= ~vm_off;
}

/* asm: asm/us/main/psyq/libsnd.s: initial SpuVoiceAttr shared by
 * _SsVmInit and SsUtAllKeyOff. */
static void vm_clear_voice(int voice)
{
    SpuVoiceAttr a = {.voice = 1u << voice, .mask = 0x60093,
        .pitch = 0x1000, .addr = 0x1000, .adsr1 = 0x80ff, .adsr2 = 0x4000};
    SpuSetVoiceAttr(&a);
}

/* asm: asm/us/main/psyq/libsnd.s: _SsVmFlush. ENVX history uses the first
 * 15 slots in the auto-off intersection, exactly as in the retail loop. */
static void vm_flush(void)
{
    env_index = (env_index + 1) & 15;
    env_history[env_index] = 0;
    for (int i = 0; i < vm_max; i++) {
        SpuGetVoiceEnvelope(i, &voices[i].env);
        if (!voices[i].env) env_history[env_index] |= 1u << i;
    }
    if (!vm_auto_off) {
        uint32_t silent = 0xffffffff;
        for (int i = 0; i < 15; i++) silent &= env_history[i];
        for (int i = 0; i < vm_max; i++) if (silent & (1u << i)) {
            if (voices[i].status == 2) {
                /* The signed low halfword is sign-extended by DW1. */
                uint32_t mask = i < 16 ? (uint32_t)(int32_t)(int16_t)(1u << i) : 1u << i;
                SpuSetNoiseVoice(0, mask);
            }
            voices[i].status = 0;
        }
    }
    vm_on &= ~vm_off;
    for (int i = 0; i < 24; i++) if (voices[i].auto_vol) vm_auto_volume(i);
    /* No reachable SsUt entry enables auto-pan. */
    for (int i = 0; i < 24; i++) {
        unsigned d = vm_dirty[i];
        SpuVoiceAttr a = {.voice = 1u << i};
        if (d & 1) { a.mask |= 3; a.volume.left = vm_regs[i][0]; a.volume.right = vm_regs[i][1]; }
        if (d & 4) { a.mask |= 0x10; a.pitch = vm_regs[i][2]; }
        if (d & 8) { a.mask |= 0x80; a.addr = (uint32_t)vm_regs[i][3] << 3; }
        if (d & 16) { a.mask |= 0x60000; a.adsr1 = vm_regs[i][4]; a.adsr2 = vm_regs[i][5]; }
        if (a.mask) SpuSetVoiceAttr(&a);
        vm_dirty[i] = 0;
    }
    SpuSetKey(0, vm_off & 0xffffff);
    SpuSetKey(1, vm_on & 0xffffff);
    SpuSetReverbVoice(8, vm_reverb & 0xffffff);
    SpuSetNoiseVoice(8, vm_noise & 0xffffff);
    vm_off = vm_on = vm_noise = 0;
}

/* asm: asm/us/main/psyq/libsnd.s: _SsVmInit(24). */
static void vm_init(void)
{
    psyq_spu_set_in_transfer(0);
    SpuInitMalloc(32, (char *)vm_alloc);
    vm_damper = 0;
    memset(vm_regs, 0, sizeof(vm_regs));
    memset(vm_dirty, 0, sizeof(vm_dirty));
    for (int i = 0; i < 16; i++) banks[i].used = 0;
    vm_max = 24;
    for (int i = 0; i < 24; i++) {
        SndVoice *v = voices + i;
        v->age = 24; v->vag = 0xff; v->status = 0;
        v->pitch = v->env = 0; v->seq = -1;
        v->compact = v->program = 0; v->tone = 0xff;
        v->volume = v->channel_volume = 0; v->pan = 64;
        v->auto_vol = v->vol_delta = v->vol_period = v->vol_count = v->vol_current = 0;
        v->auto_pan = v->pan_delta = v->pan_period = v->pan_count = v->pan_current = 0;
        vm_clear_voice(i);
        vm_key_off(i);
    }
    snd_reverb.mask = 0; snd_reverb.mode = 0;
    snd_reverb.depth.left = snd_reverb.depth.right = 0x3fff;
    vm_on = 0;
    vm_off &= 0xffff0000; /* DW1 explicitly clears only _svm_okof1. */
    vm_reverb = vm_noise = 0;
    vm_auto_off = vm_mono = 0;
    max_programs = 128;
    vm_busy = 0;
    vm_flush();
}

/* asm: asm/us/main/psyq/libsnd.s: SetAutoVol. This deliberately uses the
 * shared _svm_cur/VH context left by the most recent setup, as DW1 does. */
static void vm_auto_volume(int voice)
{
    SndVoice *v = voices + voice;
    short before = v->vol_count;
    v->vol_count--;
    if (before > 0) return;
    v->vol_current += v->vol_delta;
    if ((v->vol_delta > 0 && v->vol_current >= v->vol_target) ||
        (v->vol_delta < 0 && v->vol_current <= v->vol_target)) {
        v->vol_current = v->vol_target;
        v->auto_vol = 0;
    }
    current.volume = v->vol_current;
    uint32_t left = vm_gain(v->vol_current), right = left;
    vm_pan(&left, &right, current.tone_pan, 64);
    left = (uint16_t)left; right = (uint16_t)right;
    vm_pan(&left, &right, current.program_pan, 64);
    left = (uint16_t)left; right = (uint16_t)right;
    vm_pan(&left, &right, current.pan, 64);
    if (vm_mono) left = right = (uint16_t)left < (uint16_t)right ? right : left;
    v->vol_count = v->vol_period;
    vm_regs[voice][0] = left; vm_regs[voice][1] = right;
    vm_dirty[voice] |= 3;
}

/* asm: asm/us/main/psyq/libsnd.s: SeAutoVol. */
static void vm_set_auto_volume(int voice, short start, short end, short time)
{
    if (start == end) return;
    SndVoice *v = voices + voice;
    v->auto_vol = 1; v->vol_current = start; v->vol_target = end;
    int distance = start - end;
    int magnitude = distance < 0 ? -distance : distance;
    if (magnitude < time) {
        v->vol_delta = start > end ? -1 : 1;
        v->vol_period = v->vol_count = time / magnitude;
    } else {
        v->vol_period = v->vol_count = 0;
        v->vol_delta = -(distance / time);
    }
}
