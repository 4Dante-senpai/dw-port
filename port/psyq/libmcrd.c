/* USA mw_lib41 libmcrd state machines, translated from asm/us/main/psyq/libmcrd.s.
 * The four-level UserFunc stack and VSync slot 7 preserve command arbitration,
 * retries and callback ordering. BIOS filesystem operations live in bios_card.c. */
#include "psyq.h"
#include "bios_card.h"
#include "card_image.h"
#include "clock.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

typedef void (*MemCB)(unsigned long, unsigned long);
static struct {
	long command, result, ready, chan, fd, offset, bytes;
	void *data;
	char path[32];
	MemCB callback;
} card;
static unsigned long regs[2], first;
static long ev[2][4];
static int flags[2][4];
typedef int (*UserFn)(int *);
static UserFn users[4];
static int params[4][4], puser = -1;
/* asm: UserFuncInit */
static void UserFuncInit(void)
{
	puser = -1;
}
/* asm: UserFuncOpen */
static void UserFuncOpen(UserFn fn)
{
	if (puser == 3) {
		fprintf(stderr, "libmcrd: UserFunc overflow\n");
		return;
	}
	users[++puser] = fn;
	memset(params[puser], 0, sizeof(params[0]));
}
/* asm: UserFuncExecute */
static void UserFuncExecute(void)
{
	if (puser >= 0 && users[puser](params[puser])) {
		puser--;
	}
}
/* asm: UserFuncComplete */
static int UserFuncComplete(void)
{
	return puser < 0;
}
/* asm: _rslt_to_code */
static int _rslt_to_code(int r)
{
	switch (r) {
	case 0:
		return 0;
	case 1:
		return 2;
	case 2:
		return 1;
	case 4:
		return 3;
	default:
		return r | 0x8000;
	}
}
/* asm: funcEvSpIOE/funcEvSpError/funcEvSpTimeout/funcEvSpNewcard and x variants. */
#define EVENT_FN(name, row, col) \
    static long name(void) { flags[row][col] = 1; return 0; }
EVENT_FN(funcEvSpIOE, 0, 0)
EVENT_FN(funcEvSpError, 0, 1)
EVENT_FN(funcEvSpTimeout, 0, 2)
EVENT_FN(funcEvSpNewcard, 0, 3)
EVENT_FN(funcEvSpIOEx, 1, 0)
EVENT_FN(funcEvSpErrorx, 1, 1)
EVENT_FN(funcEvSpTimeoutx, 1, 2)
EVENT_FN(funcEvSpNewcardx, 1, 3)
#undef EVENT_FN
/* asm: _clr_card_event */
static void _clr_card_event(void)
{
	for (int r = 0; r < 2; r++) {
		for (int i = 0; i < 4; i++) {
			TestEvent(ev[r][i]);
			flags[r][i] = 0;
		}
	}
}
/* asm: _chk_card_event/_chk_card_event_x */
static int has_event(void *arg)
{
	int r = (int)(intptr_t)arg;
	return flags[r][0] + flags[r][1] + flags[r][2] + flags[r][3];
}
/* asm: _get_card_event/_get_card_event_x */
static int get_event(int r)
{
	if (!has_event((void *)(intptr_t)r)) {
		clock_wait_until(has_event, (void *)(intptr_t)r);
	}
	int code = (flags[r][0] + 2 * flags[r][1] + 4 * flags[r][2] + 8 * flags[r][3]) >> 1;
	for (int i = 0; i < 4; i++) {
		flags[r][i] = 0;
		TestEvent(ev[1 - r][i]);
	}
	return code;
}
/* asm: _card_exist_back */
static int _card_exist_back(int *s)
{
	if (!s[0]) {
		s[1] = 0;
		_clr_card_event();
		s[0] = 10;
	}
	if (s[0] == 10) {
		bios_card_info(card.chan);
		s[0] = 11;
		return 0;
	}
	if (s[0] != 11 || !has_event(0)) {
		return 0;
	}
	int raw = get_event(0);
	unsigned bit = 1u << (card.chan & 31);
	if (raw == 1 && ++s[1] < 16) {
		s[0] = 10;
		return 0;
	}
	if (raw == 0) {
		if (!(first & bit)) {
			raw = 4;
			first |= bit;
		}
	} else if (raw == 4) {
		first |= bit;
	} else {
		first &= ~bit;
	}
	card.result = _rslt_to_code(raw);
	return 1;
}
/* asm: _card_accept_back */
static int _card_accept_back(int *s)
{
	switch (s[0]) {
	case 0:
		s[1] = s[2] = 0;
		UserFuncOpen(_card_exist_back);
		s[0] = 10;
		return 0;
	case 10:
		if (card.result == 3) {
			s[2] = 1;
			_clr_card_event();
			bios_card_new();
			bios_card_write(card.chan, 63, NULL);
			s[0] = 21;
		} else if (!card.result) {
			s[0] = 30;
		} else {
			return 1;
		}
		return 0;
	case 21:
		if (!has_event((void *)1)) {
			return 0;
		}
		get_event(1);
		s[0] = 30; /* fall through */
	case 30:
		_clr_card_event();
		bios_card_load(card.chan);
		s[0] = 31;
		return 0;
	case 31: {
		if (!has_event(0)) {
			return 0;
		}
		int r = get_event(0);
		if (!r) {
			card.result = s[2] ? 3 : 0;
		} else if (r == 4) {
			card.result = 4;
		} else if (r == 1 && ++s[1] < 16) {
			s[0] = 0;
			return 0;
		} else {
			card.result = _rslt_to_code(r);
		}
		return 1;
	}
	default:
		return 0;
	}
}
/* asm: _card_read_data/_card_write_data, shared identical seek/transfer/retry states. */
static int data_back(int *s, int write)
{
	if (!s[0]) {
		s[1] = 0;
		s[0] = 10;
	}
	if (s[0] == 10) {
		if (bios_bu_seek(card.fd, card.offset) != card.offset) {
			return 0;
		}
		s[0] = 20;
	}
	if (s[0] == 20) {
		_clr_card_event();
		if (bios_bu_io(card.fd, card.data, card.bytes, write) != 0) {
			return 0;
		}
		s[0] = 30;
		return 0;
	}
	if (s[0] != 30 || !has_event(0)) {
		return 0;
	}
	int r = get_event(0);
	if (r == 1 && ++s[1] < 16) {
		s[0] = 10;
		return 0;
	}
	card.result = _rslt_to_code(r);
	return 1;
}
/* asm: _card_read_data */
static int _card_read_data(int *s)
{
	return data_back(s, 0);
}
/* asm: _card_write_data */
static int _card_write_data(int *s)
{
	return data_back(s, 1);
}
/* asm: _card_read_file/_card_write_file share the same states except read/write. */
static int file_back(int *s, int write)
{
	if (!s[0]) {
		UserFuncOpen(_card_exist_back);
		s[0] = 10;
		s[1] = 0;
		return 0;
	}
	if (s[0] == 10) {
		if (card.result) {
			return 1;
		}
		card.fd = bios_bu_open(card.path, 0x8001);
		if (card.fd < 0) {
			card.result = 5;
			return 1;
		}
		s[0] = 11;
	}
	if (s[0] == 11) {
		if (bios_bu_seek(card.fd, card.offset) != card.offset) {
			return 0;
		}
		s[0] = 20;
	}
	if (s[0] == 20) {
		_clr_card_event();
		if (bios_bu_io(card.fd, card.data, card.bytes, write) != 0) {
			return 0;
		}
		s[0] = 30;
		return 0;
	}
	if (s[0] != 30 || !has_event(0)) {
		return 0;
	}
	int r = get_event(0);
	if (r == 1 && ++s[1] < 16) {
		s[0] = 11;
		return 0;
	}
	card.result = _rslt_to_code(r);
	bios_bu_close(card.fd);
	card.fd = -1;
	return 1;
}
/* asm: _card_read_file */
static int _card_read_file(int *s)
{
	return file_back(s, 0);
}
/* asm: _card_write_file */
static int _card_write_file(int *s)
{
	return file_back(s, 1);
}
/* asm: _callback_handler. Commit the whole command before publishing completion. */
static void _callback_handler(void)
{
	if (UserFuncComplete()) {
		return;
	}
	UserFuncExecute();
	if (!UserFuncComplete()) {
		return;
	}
	if (!card.result && (card.command == 4 || card.command == 6) && card_image_commit(card.chan >> 4)) {
		card.result = 2;
	}
	card.ready = 1;
	regs[0] = card.command;
	regs[1] = card.result;
	card.command = card.result = 0;
	if (card.callback) {
		card.callback(regs[0], regs[1]);
	}
}
/* asm: _make_file_discrpt */
static void _make_file_discrpt(long chan, const char *name, char path[32])
{
	snprintf(path, 32, "bu%ld%ld:%s", (chan >> 4) & 15, chan & 15, name ? name : "");
}
/* asm: _card_start */
static void _card_start(void)
{
	long (*callbacks[2][4])(void) = { { funcEvSpIOE, funcEvSpError, funcEvSpTimeout, funcEvSpNewcard }, { funcEvSpIOEx, funcEvSpErrorx, funcEvSpTimeoutx, funcEvSpNewcardx } };
	unsigned specs[4] = { 4, 0x8000, 0x100, 0x2000 };
	EnterCriticalSection();
	for (int r = 0; r < 2; r++) {
		for (int i = 0; i < 4; i++) {
			ev[r][i] = OpenEvent(r ? CARD_HW : CARD_SW, specs[i], 0x1000, callbacks[r][i]);
			EnableEvent(ev[r][i]);
		}
	}
	_clr_card_event();
	ExitCriticalSection();
}
/* asm: _card_stop */
static void _card_stop(void)
{
	for (int r = 0; r < 2; r++) {
		for (int i = 0; i < 4; i++) {
			CloseEvent(ev[r][i]);
		}
	}
}
/* asm: MemCardInit/_card_open/InitCARD/StartCARD/_bu_init. */
void MemCardInit(long flag)
{
	clock_psyq_entry();
	(void)flag;
	first = 0;
	card.callback = NULL;
	UserFuncInit();
	card.command = card.result = card.ready = 0;
	card.fd = -1;
	bios_card_init();
	bios_bu_init();
}
/* asm: MemCardStart */
void MemCardStart(void)
{
	clock_psyq_entry();
	UserFuncInit();
	card.command = card.result = card.ready = 0;
	card.fd = -1;
	_card_start();
	VSyncCallbacks(7, _callback_handler);
}
/* asm: _card_stop/_card_close/StopCARD */
void MemCardStop(void)
{
	clock_psyq_entry();
	VSyncCallbacks(7, NULL);
	_card_stop();
	bios_card_stop();
	UserFuncInit();
}
static long begin(long chan, long command, UserFn fn)
{
	if (card.command) {
		return 0;
	}
	card.command = command;
	card.result = card.ready = 0;
	card.chan = chan;
	UserFuncOpen(fn);
	return 1;
}
/* asm: MemCardExist */
long MemCardExist(long chan)
{
	clock_psyq_entry();
	return begin(chan, 1, _card_exist_back);
}
/* asm: MemCardAccept */
long MemCardAccept(long chan)
{
	clock_psyq_entry();
	return begin(chan, 2, _card_accept_back);
}
/* asm: MemCardCallback */
MemCB MemCardCallback(MemCB fn)
{
	MemCB prev = card.callback;
	card.callback = fn;
	return prev;
}
static int completed(void *unused)
{
	(void)unused;
	return card.ready;
}
/* asm: MemCardSync. The BIOS busy wait becomes a virtual-clock wait (AD-8). */
long MemCardSync(long mode, unsigned long *command, unsigned long *result)
{
	clock_psyq_entry();
	if (!card.command && !card.ready) {
		return -1;
	}
	if (!mode && !card.ready) {
		clock_wait_until(completed, NULL);
	}
	if (command) {
		*command = card.ready ? regs[0] : card.command;
	}
	if (result) {
		*result = card.ready ? regs[1] : card.result;
	}
	if (!card.ready) {
		return 0;
	}
	card.ready = 0;
	return 1;
}
static unsigned long accept_sync(long chan)
{
	MemCB old = MemCardCallback(NULL);
	unsigned long r = 0;
	begin(chan, 2, _card_accept_back);
	MemCardSync(0, NULL, &r);
	MemCardCallback(old);
	return r;
}
/* asm: MemCardOpen */
long MemCardOpen(long chan, char *name, unsigned long flag)
{
	clock_psyq_entry();
	if (card.fd >= 0) {
		return -1;
	}
	char path[32];
	_make_file_discrpt(chan, name, path);
	for (int tries = 0;;) {
		card.fd = bios_bu_open(path, flag | 0x8000);
		if (card.fd >= 0) {
			card.chan = chan;
			return 0;
		}
		unsigned long r = accept_sync(chan);
		if (r == 3) {
			continue;
		}
		if (r == 2 && ++tries < 16) {
			continue;
		}
		return r ? r : 5;
	}
}
/* asm: MemCardReadData/MemCardWriteData */
static long transfer(long chan, char *name, long *data, long offset, long bytes, int command)
{
	if (card.command || (command < 5 ? card.fd >= 0 : card.fd < 0) || (bytes & 127)) {
		return 0;
	}
	if (command < 5) {
		_make_file_discrpt(chan, name, card.path);
	}
	card.offset = offset;
	card.bytes = bytes;
	card.data = data;
	UserFn f = command == 3 ? _card_read_file : command == 4 ? _card_write_file
	                                    : command == 5       ? _card_read_data
	                                                         : _card_write_data;
	return begin(chan, command, f);
}
/* asm: MemCardReadData */
long MemCardReadData(long *data, long offset, long bytes)
{
	clock_psyq_entry();
	return transfer(card.chan, NULL, data, offset, bytes, 5);
}
/* asm: MemCardWriteData */
long MemCardWriteData(long *data, long offset, long bytes)
{
	clock_psyq_entry();
	return transfer(card.chan, NULL, data, offset, bytes, 6);
}
/* asm: MemCardReadFile */
long MemCardReadFile(long chan, char *name, long *data, long offset, long bytes)
{
	clock_psyq_entry();
	return transfer(chan, name, data, offset, bytes, 3);
}
/* asm: MemCardWriteFile */
long MemCardWriteFile(long chan, char *name, long *data, long offset, long bytes)
{
	clock_psyq_entry();
	return transfer(chan, name, data, offset, bytes, 4);
}
/* asm: MemCardCreateFile */
long MemCardCreateFile(long chan, char *name, long blocks)
{
	clock_psyq_entry();
	if (card.command) {
		return -1;
	}
	char path[32];
	_make_file_discrpt(chan, name, path);
	first |= 1u << (card.chan & 31);
	int fd = bios_bu_open(path, 1);
	if (fd >= 0) {
		bios_bu_close(fd);
		return 6;
	}
	for (int tries = 0;;) {
		fd = bios_bu_open(path, ((unsigned)blocks << 16) | 0x200);
		if (fd >= 0) {
			bios_bu_close(fd);
			return card_image_commit(chan >> 4) ? 2 : 0;
		}
		unsigned long r = accept_sync(chan);
		if (r == 3) {
			continue;
		}
		if (r == 2 && ++tries < 16) {
			continue;
		}
		return r ? r : 7;
	}
}
/* asm: MemCardDeleteFile */
long MemCardDeleteFile(long chan, char *name)
{
	clock_psyq_entry();
	if (card.command) {
		return -1;
	}
	char path[32];
	_make_file_discrpt(chan, name, path);
	first |= 1u << (card.chan & 31);
	for (int tries = 0;;) {
		if (bios_bu_erase(path)) {
			return card_image_commit(chan >> 4) ? 2 : 0;
		}
		unsigned long r = accept_sync(chan);
		if (r == 3) {
			continue;
		}
		if (r == 2 && ++tries < 16) {
			continue;
		}
		return r ? r : 5;
	}
}
/* asm: MemCardFormat */
long MemCardFormat(long chan)
{
	clock_psyq_entry();
	if (card.command) {
		return -1;
	}
	first |= 1u << (card.chan & 31);
	_clr_card_event();
	bios_bu_format(chan);
	int r = _rslt_to_code(get_event(1));
	if (!r && card_image_commit(chan >> 4)) {
		r = 2;
	}
	return r;
}
/* asm: MemCardGetDirentry */
long MemCardGetDirentry(long chan, char *name, struct DIRENTRY *out, long *count, long offset, long max)
{
	clock_psyq_entry();
	if (card.command) {
		return -1;
	}
	char path[32];
	_make_file_discrpt(chan, name, path);
	first |= 1u << (card.chan & 31);
	BiosDir entry;
	long n = 0;
	if (offset + max > 0) {
		BiosDir *found = bios_bu_first(path, &entry);
		if (!found) {
			for (int tries = 0; tries < 16; tries++) {
				unsigned long r = accept_sync(chan);
				if (!r) {
					break;
				}
				if (tries == 15) {
					return r;
				}
				found = bios_bu_first(path, &entry);
				if (found) {
					break;
				}
			}
		}
		for (long i = 0; found && i < offset + max; i++, found = bios_bu_next(&entry)) {
			if (i >= offset && out) {
				memcpy((char *)out + n * sizeof(entry), &entry, sizeof(entry));
				n++;
			}
		}
	}
	if (count) {
		*count = n;
	}
	return 0;
}
/* asm: MemCardUnformat */
long MemCardUnformat(long chan)
{
	clock_psyq_entry();
	if (card.command) {
		return -1;
	}
	uint8_t data[128];
	memset(data, 255, 128);
	for (int i = 0; i < 15; i++) {
		_clr_card_event();
		bios_card_new();
		bios_card_write(chan, i, data);
		if (get_event(1)) {
			return 0;
		}
	}
	return card_image_commit(chan >> 4) ? 0 : 1;
}
/* asm: _patch_card/_patch_card2/_copy_memcard_patch/_dsr_start/_check_dsr/
 * _patch_wait_start/_ExitCard/_clr_vector/_c_vend are deliberately without effect:
 * native BIOS services have no SIO registers, executable vectors or RAM patches. */
