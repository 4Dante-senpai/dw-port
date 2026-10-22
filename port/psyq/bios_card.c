/*

MIT License

Copyright (c) 2021 PCSX-Redux authors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/
/* Adapted from OpenBIOS card/{backupunit,device}.c, kernel/events.c and
 * sio0/card.c at PCSX-Redux 2068784a552b598940191e8431585b7f96408b3f
 * (src/mips submodule 361f3f6). Sector events replace SIO0; no BIOS RAM patches.
 * No code from Redux src/core is used. */
#include "bios_card.h"
#include "card_image.h"
#include "clock.h"
#include "clock_costs.h"
#include "ps1mem.h"
#include <string.h>

typedef struct {
	uint32_t cls, spec;
	long mode;
	long (*fn)(void);
	int enabled, pending;
} Event;
static Event events[32];
static int masked;
static Event *event(long id)
{
	unsigned n = (unsigned)id ^ 0xf1000000u;
	return n < 32 ? &events[n] : 0;
}
/* asm: libapi.s:OpenEvent -- OpenBIOS kernel/events.c event handles. */
long OpenEvent(unsigned long cls, unsigned long spec, long mode, long (*fn)(void))
{
	for (unsigned i = 0; i < 32; i++) {
		if (!events[i].cls) {
			events[i] = (Event){ .cls = cls, .spec = spec, .mode = mode, .fn = fn };
			return 0xf1000000u | i;
		}
	}
	return -1;
}
/* asm: libapi.s:CloseEvent */
long CloseEvent(long id)
{
	Event *e = event(id);
	if (!e) {
		return 0;
	}
	memset(e, 0, sizeof(*e));
	return 1;
}
/* asm: libapi.s:EnableEvent */
long EnableEvent(long id)
{
	Event *e = event(id);
	if (!e) {
		return 0;
	}
	e->enabled = 1;
	return 1;
}
/* asm: libapi.s:DisableEvent */
long DisableEvent(long id)
{
	Event *e = event(id);
	if (!e) {
		return 0;
	}
	e->enabled = 0;
	return 1;
}
/* asm: libapi.s:TestEvent */
long TestEvent(long id)
{
	Event *e = event(id);
	if (!e || !e->enabled || !e->pending || e->mode != 0x2000) {
		return 0;
	}
	e->pending = 0;
	return 1;
}
void bios_card_deliver(uint32_t cls, uint32_t spec)
{
	for (unsigned i = 0; i < 32; i++) {
		Event *e = &events[i];
		if (e->cls != cls || e->spec != spec || !e->enabled) {
			continue;
		}
		if (e->mode == 0x1000 && !masked && e->fn) {
			e->fn();
		} else {
			e->pending = 1;
		}
	}
}
/* asm: libapi.s:EnterCriticalSection. No clock advancement while IRQs are masked. */
long EnterCriticalSection(void)
{
	int old = masked;
	masked = 1;
	return !old;
}
/* asm: libapi.s:ExitCriticalSection */
void ExitCriticalSection(void)
{
	masked = 0;
	for (unsigned i = 0; i < 32; i++) {
		if (events[i].pending && events[i].enabled && events[i].mode == 0x1000) {
			events[i].pending = 0;
			if (events[i].fn) {
				events[i].fn();
			}
		}
	}
}
typedef struct {
	uint32_t state, size;
	int16_t next;
	char name[22];
} Entry;
_Static_assert(sizeof(Entry) == 32, "OpenBIOS directory ABI");
_Static_assert(sizeof(BiosDir) == 40, "PsyQ DIRENTRY ABI");
static Entry dirs[2][15];
static uint8_t buffer[2][128];
static int fresh[2], running;
static int32_t broken[2][20];
static struct {
	int used, port, block, offset, length;
} files[16];
static struct {
	int kind, port, sector, count, fd, write, event;
	uint8_t *data;
	uint8_t copy[128];
} op;
static int enum_port, enum_index;
static char pattern[21];
static int port_of(int chan)
{
	return chan == 0 ? 0 : chan == 16 ? 1
	                                  : -1;
}
static uint8_t checksum(const uint8_t *s)
{
	uint8_t x = 0;
	for (int i = 0; i < 127; i++) {
		x ^= s[i];
	}
	return x;
}
static int formatted(int p)
{
	uint8_t *d = card_image_data(p);
	return d && d[0] == 'M' && d[1] == 'C';
}
typedef struct {
	int port, sector, write, done;
	uint8_t *data;
} Sector;
static void transfer_sector(void *arg)
{
	Sector *s = arg;
	uint8_t *image = card_image_data(s->port);
	if (image) {
		if (s->write) {
			memcpy(image + s->sector * 128, s->data, 128);
		} else {
			memcpy(s->data, image + s->sector * 128, 128);
		}
	}
	s->done = 1;
}
static int sector_complete(void *arg)
{
	return ((Sector *)arg)->done;
}
static void sector_sync(int port, int sector, uint8_t *data, int write)
{
	Sector transfer = { .port = port, .sector = sector, .data = data, .write = write };
	clock_schedule(CLOCK_CARD_SECTOR_CYCLES, transfer_sector, &transfer);
	clock_wait_until(sector_complete, &transfer);
}
static void write_entry(int p, int i)
{
	memcpy(buffer[p], &dirs[p][i], 32);
	buffer[p][127] = checksum(buffer[p]);
	sector_sync(p, i + 1, buffer[p], 1);
}
/* OpenBIOS buValidateEntryAndCorrect followed by orphan accounting. Bounds
 * checks make corrupt chains fail safely instead of indexing outside the TOC. */
static int repair_dirs(int p, int include_deleted)
{
	Entry *entries = dirs[p];
	for (int i = 0; i < 15; i++) {
		Entry *e = &entries[i];
		int mask = e->state == 0x51 ? 0xa0 : e->state == 0xa1 ? 0x50
		                                                      : 0;
		int count = ((int32_t)e->size + ((int32_t)e->size < 0 ? 8191 : 0)) / 8192 - 1;
		int b = e->next;
		if (!mask || count <= 0 || b == -1) {
			continue;
		}
		int remaining = count;
		while (b >= 0 && b < 15 && (entries[b].state & 0xf0) != (unsigned)mask) {
			b = entries[b].next;
			if (--remaining < 0 || b == -1) {
				break;
			}
		}
		if (b == -1 || remaining < 0) {
			continue;
		}
		if (b < 0 || b >= 15) {
			return 0;
		}
		e->name[0] = 0;
		e->state = 0xa0;
		e->size = 0;
		b = e->next;
		e->next = -1;
		while (count-- > 0 && b != -1) {
			if (b < 0 || b >= 15) {
				return 0;
			}
			Entry *tail = &entries[b];
			b = tail->next;
			tail->state = 0xa0;
			tail->size = 0;
			tail->next = -1;
		}
	}
	unsigned referenced[15] = { 0 };
	for (int i = 0; i < 15; i++) {
		Entry *e = &entries[i];
		if (e->state != 0x51 && !(include_deleted && e->state == 0xa1)) {
			continue;
		}
		referenced[i] = 1;
		int b = e->next;
		int count = ((int32_t)e->size + ((int32_t)e->size < 0 ? 8191 : 0)) / 8192;
		if (count > 15) {
			return 0;
		}
		while (--count > 0 && b != -1) {
			if (b < 0 || b >= 15) {
				return 0;
			}
			referenced[b]++;
			b = entries[b].next;
		}
	}
	for (int i = 0; i < 15; i++) {
		if (!referenced[i]) {
			entries[i].state = 0xa0;
			entries[i].size = 0;
			entries[i].next = -1;
		}
	}
	return 1;
}
static int path_port(const char *path)
{
	if (!strncmp(path, "bu00:", 5)) {
		return 0;
	}
	if (!strncmp(path, "bu10:", 5)) {
		return 1;
	}
	return -1;
}
static int matches(const char *name, const char *pat)
{
	for (int i = 0; i < 20; i++) {
		if (pat[i] == '*') {
			return 1;
		}
		if (!name[i]) {
			return !pat[i] || pat[i] == '?';
		}
		if (!pat[i] || (pat[i] != '?' && pat[i] != name[i])) {
			return 0;
		}
	}
	return 1;
}
static int find(int p, const char *pat, int from)
{
	for (int i = from; i < 15; i++) {
		if (dirs[p][i].state == 0x51 && matches(dirs[p][i].name, pat)) {
			return i;
		}
	}
	return -1;
}
/* OpenBIOS buInit: mirror the header at sector 63, then read TOC and broken list. */
static int bu_init(int p)
{
	if (!formatted(p)) {
		return 0;
	}
	fresh[p] = 0;
	sector_sync(p, 0, buffer[p], 0);
	sector_sync(p, 63, buffer[p], 1);
	for (int i = 1; i < 16; i++) {
		sector_sync(p, i, buffer[p], 0);
		if (checksum(buffer[p]) != buffer[p][127]) {
			return 0;
		}
		memcpy(&dirs[p][i - 1], buffer[p], 32);
	}
	if (!repair_dirs(p, 1)) {
		return 0;
	}
	for (int i = 16; i < 36; i++) {
		sector_sync(p, i, buffer[p], 0);
		if (checksum(buffer[p]) != buffer[p][127]) {
			return 0;
		}
		memcpy(&broken[p][i - 16], buffer[p], 4);
	}
	return 1;
}
void bios_bu_init(void)
{
	for (int p = 0; p < 2; p++) {
		bu_init(p);
	}
}
void bios_card_init(void)
{
	clock_cancel(op.event);
	memset(&op, 0, sizeof(op));
	memset(files, 0, sizeof(files));
	fresh[0] = fresh[1] = 1;
	running = 1;
}
void bios_card_stop(void)
{
	running = 0;
	clock_cancel(op.event);
	memset(&op, 0, sizeof(op));
}
void bios_card_new(void)
{ /* BIOS permits subsequent sector writes after a new-card response. */
}
static int absolute(int fd, int sector)
{
	int p = files[fd].port, b = files[fd].block;
	for (int n = sector / 64; n > 0; n--) {
		if (b < 0 || b >= 15) {
			return -1;
		}
		b = dirs[p][b].next;
	}
	if (b < 0 || b >= 15) {
		return -1;
	}
	int absolute = (b + 1) * 64 + (sector & 63);
	for (int i = 0; i < 20; i++) {
		if (broken[p][i] == absolute) {
			return 36 + i;
		}
	}
	return absolute;
}
static void finish(unsigned spec)
{
	int kind = op.kind;
	op.kind = 0;
	op.event = 0;
	bios_card_deliver(CARD_HW, spec);
	if (kind != 3) {
		bios_card_deliver(CARD_SW, spec);
	}
}
static void sector_done(void *unused)
{
	(void)unused;
	int p = op.port;
	uint8_t *d = card_image_data(p);
	if (!running || !d) {
		finish(0x100);
		return;
	}
	if (op.kind == 1) {
		unsigned spec = fresh[p] ? 0x2000 : 4;
		fresh[p] = 0;
		finish(spec);
		return;
	}
	if (op.kind == 2) {
		memcpy(buffer[p], d + op.sector * 128, 128);
		if (op.sector == 0 && !formatted(p)) {
			finish(0x2000);
			return;
		}
		if (op.sector > 0 && checksum(buffer[p]) != buffer[p][127]) {
			/* Async buReadTOC retains default entries on bad checksums. */
			if (op.sector <= 15) {
				memset(&dirs[p][op.sector - 1], 0, 32);
				dirs[p][op.sector - 1].state = 0xa0;
				dirs[p][op.sector - 1].next = -1;
			}
		}
		if (op.sector >= 1 && op.sector <= 15 && checksum(buffer[p]) == buffer[p][127]) {
			memcpy(&dirs[p][op.sector - 1], buffer[p], 32);
		}
		if (op.sector == 15 && !repair_dirs(p, 0)) {
			finish(0x8000);
			return;
		}
		if (op.sector >= 16) {
			broken[p][op.sector - 16] = -1;
			if (checksum(buffer[p]) == buffer[p][127]) {
				memcpy(&broken[p][op.sector - 16], buffer[p], 4);
			}
		}
		if (++op.sector < 36) {
			op.event = clock_schedule(CLOCK_CARD_SECTOR_CYCLES, sector_done, 0);
			return;
		}
		finish(4);
		return;
	}
	if (op.kind == 3) {
		memcpy(d + op.sector * 128, op.copy, 128);
		fresh[p] = 0;
		finish(4);
		return;
	}
	int s = absolute(op.fd, op.sector);
	if (s < 0) {
		finish(0x8000);
		return;
	}
	if (op.write) {
		memcpy(d + s * 128, op.data, 128);
	} else {
		memcpy(op.data, d + s * 128, 128);
	}
	/* OpenBIOS increments read offset for all but the final sector; write also
	 * increments once after its final card-info response. */
	if (op.write || op.count > 1) {
		files[op.fd].offset += 128;
	}
	op.data += 128;
	op.sector++;
	if (--op.count) {
		op.event = clock_schedule(CLOCK_CARD_SECTOR_CYCLES, sector_done, 0);
	} else {
		finish(4);
	}
}
static int start(int kind, int chan)
{
	int p = port_of(chan);
	if (p < 0 || op.kind || !running) {
		return 0;
	}
	op.kind = kind;
	op.port = p;
	op.sector = 0;
	op.event = clock_schedule(CLOCK_CARD_COMMAND_CYCLES + CLOCK_CARD_SECTOR_CYCLES, sector_done, 0);
	return 1;
}
/* asm: libmcrd.s:_card_info/_card_load/_card_write BIOS trampolines. */
int bios_card_info(int c)
{
	return start(1, c);
}
int bios_card_load(int c)
{
	return start(2, c);
}
int bios_card_write(int c, int s, const uint8_t *data)
{
	if (s < 0 || s >= 1024 || !start(3, c)) {
		return 0;
	}
	op.sector = s;
	if (data) {
		memcpy(op.copy, data, 128);
	} else {
		memcpy(op.copy, PS1_RAM(0), 128);
	}
	return 1;
}
int bios_bu_open(const char *path, unsigned mode)
{
	int p = path_port(path);
	if (p < 0 || op.kind || !card_image_data(p)) {
		return -1;
	}
	if (!(mode & 0x8000) && !bu_init(p)) {
		return -1;
	}
	int i = find(p, path + 5, 0);
	if (mode & 0x200) {
		if (i >= 0) {
			return -1;
		}
		unsigned count = (mode >> 16) & 65535, free_count = 0;
		int allocated[15], n = 0;
		for (int b = 0; b < 15; b++) {
			if ((dirs[p][b].state & 0xf0) == 0xa0) {
				free_count++;
			}
		}
		if (!count || count > free_count) {
			return -1;
		}
		for (int b = 0; b < 15 && n < (int)count; b++) {
			if ((dirs[p][b].state & 0xf0) == 0xa0) {
				allocated[n++] = b;
			}
		}
		for (int k = 0; k < n; k++) {
			Entry *e = &dirs[p][allocated[k]];
			e->state = k ? ((k == n - 1) ? 0x53 : 0x52) : 0x51;
			e->next = k + 1 < n ? allocated[k + 1] : -1;
			if (!k) {
				e->size = count * 8192;
				strncpy(e->name, path + 5, 20);
			}
		}
		for (int k = 1; k < n; k++) {
			write_entry(p, allocated[k]);
		}
		write_entry(p, allocated[0]);
		i = allocated[0];
	}
	if (i < 0) {
		return -1;
	}
	for (int fd = 0; fd < 16; fd++) {
		if (!files[fd].used) {
			files[fd].used = 1;
			files[fd].port = p;
			files[fd].block = i;
			files[fd].offset = 0;
			files[fd].length = dirs[p][i].size;
			return fd;
		}
	}
	return -1;
}
int bios_bu_close(int fd)
{
	if (fd < 0 || fd >= 16 || !files[fd].used) {
		return -1;
	}
	files[fd].used = 0;
	return 0;
}
int bios_bu_seek(int fd, int offset)
{
	if (fd < 0 || fd >= 16 || !files[fd].used || offset < 0) {
		return -1;
	}
	return files[fd].offset = offset;
}
int bios_bu_io(int fd, void *data, int size, int write)
{
	if (fd < 0 || fd >= 16 || !files[fd].used || size < 0 || !data || op.kind) {
		return -1;
	}
	int off = files[fd].offset;
	if ((off & 127) || off >= files[fd].length || size > files[fd].length - off) {
		return -1;
	}
	if (!size) {
		bios_card_deliver(CARD_SW, 4);
		return 0;
	}
	if (!start(4, files[fd].port * 16)) {
		return -1;
	}
	op.fd = fd;
	op.sector = off / 128;
	op.count = size / 128;
	op.data = data;
	op.write = write;
	return 0;
}
int bios_bu_erase(const char *path)
{
	int p = path_port(path);
	if (p < 0 || op.kind || !bu_init(p)) {
		return 0;
	}
	int first = find(p, path + 5, 0);
	if (first < 0) {
		return 0;
	}
	int chain[15], n = 0, b = first;
	while (b >= 0 && b < 15 && n < 15) {
		chain[n++] = b;
		b = dirs[p][b].next;
	}
	for (int k = 0; k < n; k++) {
		dirs[p][chain[k]].state += 0x50;
	}
	for (int k = 1; k < n; k++) {
		write_entry(p, chain[k]);
	}
	write_entry(p, first);
	return 1;
}
int bios_bu_format(int chan)
{
	int p = port_of(chan);
	if (p < 0 || !card_image_data(p)) {
		bios_card_deliver(CARD_HW, 0x100);
		return 0;
	}
	memset(buffer[p], 0, 128);
	buffer[p][0] = 'M';
	buffer[p][1] = 'C';
	buffer[p][127] = checksum(buffer[p]);
	sector_sync(p, 0, buffer[p], 1);
	for (int i = 0; i < 15; i++) {
		memset(&dirs[p][i], 0, 32);
		dirs[p][i].state = 0xa0;
		dirs[p][i].next = -1;
		write_entry(p, i);
	}
	for (int i = 16; i < 36; i++) {
		memset(buffer[p], 255, 4);
		buffer[p][127] = checksum(buffer[p]);
		sector_sync(p, i, buffer[p], 1);
		broken[p][i - 16] = -1;
	}
	fresh[p] = 0;
	bios_card_deliver(CARD_HW, 4);
	return 1;
}
BiosDir *bios_bu_next(BiosDir *out)
{
	int b = find(enum_port, pattern, enum_index);
	if (b < 0) {
		return 0;
	}
	enum_index = b + 1;
	memset(out, 0, sizeof(*out));
	memcpy(out->name, dirs[enum_port][b].name, 20);
	out->attr = dirs[enum_port][b].state & 0xf0;
	out->size = dirs[enum_port][b].size;
	out->head = (b + 1) * 64;
	return out;
}
BiosDir *bios_bu_first(const char *path, BiosDir *out)
{
	int p = path_port(path);
	if (p < 0 || op.kind || !bu_init(p)) {
		return 0;
	}
	enum_port = p;
	enum_index = 0;
	strncpy(pattern, path + 5, 20);
	pattern[20] = 0;
	return bios_bu_next(out);
}

/* asm: libmcrd.s:InitCARD/InitCARD2. PAD/BIOS patching has no native side effect. */
void InitCARD(long flag)
{
	(void)flag;
	bios_card_init();
}
/* asm: libmcrd.s:StartCARD/StartCARD2 */
long StartCARD(void)
{
	running = 1;
	return 1;
}
/* asm: libmcrd.s:StopCARD/StopCARD2 */
long StopCARD(void)
{
	bios_card_stop();
	return 1;
}
/* asm: libmcrd.s:InitCARD2 -- native equivalent of the BIOS trampoline. */
void InitCARD2(long flag)
{
	InitCARD(flag);
}
/* asm: libmcrd.s:StartCARD2 */
long StartCARD2(void)
{
	return StartCARD();
}
/* asm: libmcrd.s:StopCARD2 */
long StopCARD2(void)
{
	return StopCARD();
}
/* asm: libapi.s:_bu_init */
void _bu_init(void)
{
	bios_bu_init();
}
/* asm: libmcrd.s:_card_info */
long _card_info(long chan)
{
	return bios_card_info(chan);
}
/* asm: libmcrd.s:_card_load */
long _card_load(long chan)
{
	return bios_card_load(chan);
}
/* asm: libmcrd.s:_card_write */
long _card_write(long chan, long sector, unsigned char *data)
{
	return bios_card_write(chan, sector, data);
}
/* asm: libmcrd.s:_new_card */
void _new_card(void)
{
	bios_card_new();
}
/* asm: libmcrd.s:_card_clear */
void _card_clear(long chan)
{
	bios_card_new();
	bios_card_write(chan, 63, NULL);
}
/* asm: libmcrd.s:_patch_card. No BIOS machine code exists to patch. */
void _patch_card(void)
{
}
/* asm: libmcrd.s:_patch_card2 */
void _patch_card2(void)
{
}
/* asm: libmcrd.s:_copy_memcard_patch */
void _copy_memcard_patch(void)
{
}
/* asm: libmcrd.s:_dsr_start */
void _dsr_start(void)
{
}
/* asm: libmcrd.s:_check_dsr */
void _check_dsr(void)
{
}
/* asm: libmcrd.s:_patch_wait_start */
void _patch_wait_start(void)
{
}
/* asm: libmcrd.s:_ExitCard -- clears an executable BIOS vector on PS1 only.
 * _clr_vector/_c_vend delimit that zero-filled patch payload, not C routines. */
void _ExitCard(void)
{
}
