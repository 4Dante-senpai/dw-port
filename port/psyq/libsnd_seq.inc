/* DW1 mw_lib41 sequence records. asm: asm/us/main/psyq/libsnd.s. */
#include <stdio.h>

/* asm: asm/us/main/psyq/libsnd.s: _SsReadDeltaValue. SEQ deltas are
 * multiplied by ten and accumulated with 32-bit wrapping arithmetic. */
static uint32_t snd_delta(SndSeq *q)
{
    unsigned byte = *q->cursor++;
    uint32_t value = byte & 127;
    while (byte & 128) {
        byte = *q->cursor++;
        value = (value << 7) + (byte & 127);
    }
    value *= 10;
    q->elapsed = (uint32_t)q->elapsed + value;
    return value;
}

/* asm: asm/us/main/psyq/libsnd.s: shared channel defaults in
 * _SsInitSoundSeq / _SsSndStop. */
static void snd_channel_defaults(SndSeq *q)
{
    for (int i = 0; i < 16; i++) {
        q->state[0x37 - 0x14 + i] = i;
        q->state[0x27 - 0x14 + i] = 64;
        q->channel_volume[i] = 127;
    }
}

/* asm: asm/us/main/psyq/libsnd.s: byte resets common to initialization
 * and stop. Unlisted bytes and the sequence's stereo volume survive. */
static void snd_reset_status(SndSeq *q)
{
    for (int i = 0x14; i <= 0x1f; i++) q->state[i - 0x14] = 0;
    q->state[0x21 - 0x14] = 0;
    q->elapsed = 0;
}

/* asm: asm/us/main/psyq/libsnd.s: _SsInitSoundSeq. */
static int snd_init_seq(SndSeq *q, unsigned char *data, short vab)
{
    q->state[0x26 - 0x14] = vab;
    q->resolution = 0;
    snd_reset_status(q);
    q->delta = q->tempo = q->initial_delta = 0;
    q->step = 0;
    q->state[0x20 - 0x14] = 1;
    q->play_count = 0;
    q->state[0x24 - 0x14] = q->state[0x25 - 0x14] = 0;
    snd_channel_defaults(q);
    q->repeat = 1;
    q->cursor = data;
    if (data[0] != 0x53 && data[0] != 0x70) {
        fputs("This is an old SEQ Data Format.\n", stdout);
        return 0;
    }
    q->cursor = data + 8;
    if (data[7] != 1) { fputs("This is not SEQ Data.\n", stdout); return -1; }
    q->resolution = (data[8] << 8) | data[9];
    uint32_t us = (data[10] << 16) | (data[11] << 8) | data[12];
    /* Retail traps for zero divisors; reject malformed synthetic input. */
    if (!us || !q->resolution || !tick.rate) return -1;
    q->tempo = 60000000 / us + (60000000 % us > us / 2);
    q->initial_tempo = q->tempo;
    q->state[0x24 - 0x14] = data[13]; q->state[0x25 - 0x14] = data[14];
    q->cursor = data + 15;
    q->delta = q->initial_delta = snd_delta(q);
    q->loop = q->next = q->start = q->cursor; q->saved = NULL;
    uint32_t product = (uint32_t)(int32_t)q->resolution * q->tempo;
    if (!product) return -1;
    uint32_t numerator = product * 10, denominator = (uint32_t)tick.rate * 60;
    if (numerator < denominator) q->repeat = q->interval = ((uint32_t)tick.rate * 600) / product;
    else {
        q->repeat = -1;
        q->interval = numerator / denominator + (numerator % denominator > (uint32_t)tick.rate * 30);
    }
    q->step = q->interval;
    return 0;
}

/* asm: asm/us/main/psyq/libsnd.s: _SsVmSeqKeyOff. */
static void vm_seq_key_off(short id)
{
    for (int i = 0; i < vm_max; i++)
        if (!(vm_reserved & (1u << i)) && voices[i].seq == id) vm_key_off(i);
}

/* asm: asm/us/main/psyq/libsnd.s: _SsSndStop / _SsVmDamperOff. */
static void snd_stop(short id, short sep)
{
    SndSeq *q = scores[id] + sep;
    q->flags = (q->flags & ~(1 | 2 | 8 | 0x400)) | 4;
    vm_seq_key_off(id | (sep << 8));
    vm_damper = 0;
    snd_reset_status(q);
    q->initial_delta = q->delta; q->initial_tempo = q->tempo;
    q->interval = q->step; q->cursor = q->loop = q->start;
    snd_channel_defaults(q);
    q->volume[2] = q->volume[3] = 127;
}

/* asm: asm/us/main/psyq/libsnd.s: _SsVmSetSeqVol. The fourth argument
 * is unused in this DW1 implementation. Volume changes of active SEQ voices
 * use squared gain, unlike the explicit SsUtKeyOnV path. */
static short vm_seq_volume(short id, unsigned short left, unsigned short right)
{
    SndSeq *q = scores[id & 255] + ((unsigned short)id >> 8);
    q->volume[0] = left >= 127 ? 127 : left;
    q->volume[1] = right >= 127 ? 127 : right;
    for (int i = 0; i < vm_max; i++) {
        SndVoice *v = voices + i;
        if ((vm_reserved & (1u << i)) || v->seq != id || v->vab != (signed char)q->state[0x26 - 0x14]) continue;
        /* DW1 passes compact here, not program. Preserve that distinction. */
        if (vm_setup(v->vab, v->compact) < 0) continue;
        if ((unsigned short)v->channel_volume >= 16 || v->program < 0 || v->program >= max_programs) continue;
        int level = (v->volume * q->channel_volume[v->channel_volume]) / 127;
        int32_t product = (uint32_t)level * 16383u * current_bank->header[0x18];
        int32_t base = product / 16129;
        unsigned char *p = current_bank->programs + v->program * 16;
        int index = v->compact * 16 + v->tone;
        if (index < 0 || index >= vh16(current_bank->header + 0x12) * 16) continue;
        unsigned char *t = current_bank->tones + index * 32;
        uint32_t gain = ((uint32_t)base * p[1] * t[2]) / 16129;
        uint32_t l = gain * (unsigned short)q->volume[0] / 127;
        uint32_t r = gain * (unsigned short)q->volume[1] / 127;
        vm_pan(&l, &r, t[3], 63);
        l = (uint16_t)l; r = (uint16_t)r;
        vm_pan(&l, &r, p[4], 63);
        l = (uint16_t)l; r = (uint16_t)r;
        vm_pan(&l, &r, v->pan, 63);
        l = (uint16_t)l; r = (uint16_t)r;
        if (vm_mono) l = r = l < r ? r : l;
        vm_regs[i][0] = (int32_t)(l * l) / 16383;
        vm_regs[i][1] = (int32_t)(r * r) / 16383;
        vm_dirty[i] |= 3;
    }
    return id;
}
