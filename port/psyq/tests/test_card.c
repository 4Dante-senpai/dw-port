#include "../psyq.h"
#include "../bios_card.h"
#include "../card_image.h"
#include "../clock.h"
#include "../../host/pack/sha256.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifndef __EMSCRIPTEN__
static unsigned commits;
static int commit_fails;
static int commit(void *arg, const uint8_t *data, size_t size)
{
	(void)arg;
	(void)data;
	assert(size == 131072);
	commits++;
	return commit_fails;
}
#endif
static unsigned event_calls;
static long event_callback(void)
{
	event_calls++;
	return 0;
}
static unsigned long sync_result(unsigned long command)
{
	unsigned long c = 99, r = 99;
	assert(MemCardSync(0, &c, &r) == 1);
	assert(c == command);
	assert(MemCardSync(1, 0, 0) == -1);
	return r;
}
static void init(void)
{
	clock_reset();
	ResetCallback();
	MemCardInit(1);
	MemCardStart();
}
#ifndef __EMSCRIPTEN__
static unsigned called;
static unsigned long callback_result;
static void callback(unsigned long c, unsigned long r)
{
	assert(c == 4 && r == callback_result);
	assert(commits == 2 + called);
	called++;
}
static void directory_entry(uint8_t *image, unsigned block, unsigned state, int next, unsigned size)
{
	uint8_t *entry = image + (block + 1) * 128;
	memset(entry, 0, 128);
	entry[0] = state;
	for (unsigned i = 0; i < 4; i++) entry[4 + i] = size >> (8 * i);
	entry[8] = next;
	entry[9] = (unsigned)next >> 8;
	memcpy(entry + 10, "BROKEN", 7);
	for (unsigned i = 0; i < 127; i++) entry[127] ^= entry[i];
}
static void malformed_directories(void)
{
	static uint8_t image[CARD_IMAGE_SIZE];
	long output[4096];
	/* Out-of-range positive/negative links, a cycle, and an early terminator.
	 * Checksums stay valid so directory traversal, not checksum rejection, is tested. */
	const int links[] = {15, 32767, -2, 1, -1};
	for (unsigned i = 0; i < sizeof(links) / sizeof(*links); i++) {
		card_image_new(image);
		directory_entry(image, 0, 0x51, links[i], 3 * 8192);
		if (links[i] == 1) directory_entry(image, 1, 0x52, 0, 0);
		card_image_attach(0, image, NULL, NULL);
		init();
		long r = MemCardOpen(0, "BROKEN", 1);
		assert(r == 0 || r == 2 || r == 5);
		if (!r) {
			assert(MemCardReadData(output, 8192, 16384));
			r = sync_result(5);
			assert(r == 0 || r == 2 || r == 5);
		}
		MemCardStop();
		init();
		assert(MemCardExist(0));
		r = sync_result(1);
		assert(r == 0 || r == 3);
		assert(MemCardReadFile(0, "BROKEN", output, 8192, 16384));
		r = sync_result(3);
		assert(r == 0 || r == 2 || r == 5);
		MemCardStop();
		card_image_detach(0);
	}
}
#endif
int main(void)
{
	static uint8_t blank[CARD_IMAGE_SIZE];
	card_image_new(blank);
	Sha256 sha;
	uint8_t digest[32];
	char hex[65];
	Sha256_Init(&sha);
	Sha256_Update(&sha, blank, sizeof(blank));
	Sha256_Final(&sha, digest);
	for (int i = 0; i < 32; i++) {
		sprintf(hex + 2 * i, "%02x", digest[i]);
	}
	assert(!strcmp(hex, "78b6d4ac9ab4d23caf7e5f04f83539bf5d994cccfb0a709d14ac53d05c8e21ef"));
	init();
	assert(MemCardExist(0));
	assert(!MemCardAccept(0));
	assert(MemCardSync(1, 0, 0) == 0);
	assert(sync_result(1) == 1);
	assert(MemCardAccept(16));
	assert(sync_result(2) == 1);
	long out[2048];
#ifndef __EMSCRIPTEN__
	long data[2048];
	for (unsigned i = 0; i < 2048; i++) {
		data[i] = (long)(i * 0x1020304u);
	}
#endif
	assert(!MemCardReadFile(0, "SAVE", out, 0, 4));
	assert(MemCardReadFile(0, "SAVE", out, 0, 128));
	assert(sync_result(3) == 1);
#ifdef __EMSCRIPTEN__
	card_image_attach(0, blank, 0, 0);
	assert(!card_image_data(0));
#else
	card_image_attach(0, blank, commit, 0);
	init();
	assert(MemCardExist(0));
	assert(sync_result(1) == 3);
	assert(MemCardAccept(0));
	assert(sync_result(2) == 0);
	assert(MemCardCreateFile(0, "SAVE", 1) == 0);
	assert(commits == 1);
	assert(MemCardCreateFile(0, "SAVE", 1) == 6);
	MemCardCallback(callback);
	assert(MemCardWriteFile(0, "SAVE", data, 0, 8192));
	assert(!MemCardExist(0));
	assert(sync_result(4) == 0);
	assert(called == 1);
	commit_fails = 1;
	callback_result = 2;
	assert(MemCardWriteFile(0, "SAVE", data, 0, 8192));
	assert(sync_result(4) == 2);
	assert(called == 2 && commits == 3);
	commit_fails = 0;
	MemCardCallback(0);
	assert(MemCardReadFile(0, "SAVE", out, 0, 8192));
	assert(sync_result(3) == 0);
	assert(!memcmp(data, out, 8192));
	assert(!memcmp(card_image_data(0) + 8192, data, 8192));
	BiosDir dir[15];
	long count = -1;
	assert(MemCardGetDirentry(0, "*", (struct DIRENTRY *)dir, &count, 0, 15) == 0);
	assert(count == 1);
	assert(!strcmp(dir[0].name, "SAVE") && dir[0].size == 8192 && dir[0].head == 64 && dir[0].attr == 0x50);
	assert(MemCardGetDirentry(0, "NOMATCH", (struct DIRENTRY *)dir, &count, 0, 15) == 0 && count == 0);
	assert(MemCardGetDirentry(0, "S?VE", (struct DIRENTRY *)dir, &count, 1, 15) == 0 && count == 0);
	assert(MemCardDeleteFile(0, "SAVE") == 0);
	assert(card_image_data(0)[128] == 0xa1);
	assert(MemCardDeleteFile(0, "SAVE") == 5);
	assert(MemCardCreateFile(0, "FULL", 15) == 0);
	assert(MemCardCreateFile(0, "OVERFLOW", 1) == 7);
	assert(MemCardWriteFile(0, "FULL", data, 8192, 8192));
	assert(sync_result(4) == 0);
	assert(MemCardReadFile(0, "FULL", out, 8192, 8192));
	assert(sync_result(3) == 0);
	assert(!memcmp(data, out, 8192));
	assert(MemCardDeleteFile(0, "FULL") == 0);
	assert(MemCardFormat(0) == 0);
	assert(card_image_data(0)[128] == 0xa0);
	assert(!memcmp(card_image_data(0) + 16384, data, 8192));
	assert(MemCardGetDirentry(0, "*", (struct DIRENTRY *)dir, &count, 0, 15) == 0 && count == 0);
	/* An established device reads sector zero, retaining its cached TOC.
	 * Reloading all 37 TOC/broken-list sectors misses menu input windows. */
	BiosDir cached;
	uint64_t directory_start = clock_now();
	assert(!bios_bu_first("bu00:*", &cached));
	assert(clock_now() - directory_start < 4u * ((uint64_t)CLOCK_HZ * CLOCK_VBLANK_NUM / CLOCK_VBLANK_DEN));
	assert(MemCardCreateFile(0, "OPEN", 1) == 0);
	assert(MemCardOpen(0, "OPEN", 1) == 0);
	assert(MemCardWriteData(data, 0, 8192));
	assert(sync_result(6) == 0);
	assert(MemCardReadData(out, 0, 8192));
	assert(sync_result(5) == 0);
	assert(!memcmp(data, out, 8192));
	MemCardStop();
	card_image_detach(0);
	card_image_attach(1, blank, commit, 0);
	init();
	assert(MemCardExist(16));
	assert(sync_result(1) == 3);
	assert(MemCardCreateFile(16, "SECOND", 1) == 0);
	assert(MemCardCreateFile(0, "ABSENT", 1) == 1);
	MemCardStop();
	card_image_detach(1);
	malformed_directories();
#endif
	long e = OpenEvent(CARD_SW, 4, 0x2000, 0);
	assert(e != -1);
	EnableEvent(e);
	bios_card_deliver(CARD_SW, 4);
	assert(TestEvent(e) == 1);
	assert(TestEvent(e) == 0);
	DisableEvent(e);
	bios_card_deliver(CARD_SW, 4);
	assert(!TestEvent(e));
	CloseEvent(e);
	e = OpenEvent(CARD_HW, 4, 0x1000, event_callback);
	EnableEvent(e);
	assert(EnterCriticalSection() == 1);
	bios_card_deliver(CARD_HW, 4);
	assert(!event_calls);
	ExitCriticalSection();
	assert(event_calls == 1);
	CloseEvent(e);
	puts("card: blank oracle, absent/busy, filesystem and command roundtrip OK");
	return 0;
}
