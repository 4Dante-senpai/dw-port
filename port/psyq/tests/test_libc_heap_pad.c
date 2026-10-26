/* L1: BIOS random sequence, asm heap addresses and headless pad state. */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../clock.h"
#include "../ps1mem.h"
#include "../psyq.h"

static int failures;
#define CHECK(cond) do { \
	if (!(cond)) { \
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		failures++; \
	} \
} while (0)

static void bios_libc_costs(void)
{
	char bytes[668], copy[92];
	uint64_t start = clock_now();
	CHECK(psyq_memset(bytes, 'a', sizeof bytes) == bytes);
	CHECK(clock_now() - start == 62239 && bytes[667] == 'a');
	start = clock_now();
	CHECK(psyq_memcpy(copy, bytes, sizeof copy) == copy);
	CHECK(clock_now() - start == 10443 && !memcmp(copy, bytes, sizeof copy));
	start = clock_now();
	CHECK(psyq_strcmp("abx", "aby") < 0);
	CHECK(clock_now() - start == 497);
	start = clock_now();
	CHECK(psyq_strcmp("abcde", "abcde") == 0);
	CHECK(clock_now() - start == 1009);
	const char *path = "abc\\defg";
	start = clock_now();
	CHECK(psyq_strrchr(path, '\\') == path + 3);
	CHECK(clock_now() - start == 1622);
	CHECK(psyq_strrchr(path, '\0') == path + strlen(path));
	const char high_bytes[] = { (char)0x80, 'a', (char)0x80, 0 };
	CHECK(psyq_strrchr(high_bytes, 128) == high_bytes + 2);
}

static void random_sequences(void)
{
	/* Independent golden generation (Python, not the implementation under test):
python3 -c 'from itertools import accumulate as a; from functools import reduce; print([(hex(s), (v := [(x >> 16) & 32767 for x in a(range(10000), lambda x, _: (x * 1103515245 + 12345) & 0xffffffff, initial=s)][1:])[:5], reduce(lambda h, x: (h * 31 + x) & 0xffffffff, v, 0)) for s in (1, 0, 0x24040001)])'
	 * digest is order-sensitive: h = h * 31 + value (mod 2^32).
	 */
	static const struct {
		unsigned int seed;
		int first[5];
		uint32_t digest;
	} golden[] = {
		{ 1, { 16838, 5758, 10113, 17515, 31051 }, 3636028843u },
		{ 0, { 0, 21468, 9988, 22117, 3498 }, 3637297253u },
		{ 0x24040001, { 20346, 17442, 3669, 9391, 11839 }, 4256336683u },
	};
	uint64_t before = clock_now();
	/* Check static initialization before calling either srand or the reset hook. */
	CHECK(rand() == 20346);
	CHECK(rand() == 17442);
	for (size_t s = 0; s < sizeof golden / sizeof golden[0]; s++) {
		srand(golden[s].seed);
		uint32_t digest = 0;
		for (int i = 0; i < 10000; i++) {
			int value = rand();
			CHECK(value >= 0 && value <= 0x7FFF);
			if (i < 5)
				CHECK(value == golden[s].first[i]);
			digest = digest * 31u + (uint32_t)value;
		}
		CHECK(digest == golden[s].digest);
	}
	psyq_libc_reset();
	CHECK(rand() == 20346);
	CHECK(rand() == 17442);
	CHECK(clock_now() == before);
}

static void init_heap(uint32_t addr, unsigned long size)
{
	uint64_t before = clock_now();
	InitHeap3((unsigned long *)(uintptr_t)addr, size);
	CHECK(clock_now() == before + CLOCK_PSYQ_ENTRY_CYCLES);
}

static void *alloc(size_t size, uint32_t expected)
{
	uint64_t before = clock_now();
	void *p = malloc3(size);
	CHECK(clock_now() == before + CLOCK_PSYQ_ENTRY_CYCLES);
	CHECK(p == (expected ? ps1_ram(expected) : NULL));
	return p;
}

static void release(void *p)
{
	uint64_t before = clock_now();
	free3(p);
	CHECK(clock_now() == before + CLOCK_PSYQ_ENTRY_CYCLES);
}

static void heap_addresses(void)
{
	/* Hand-derived from libapi.s: 0x1000 / 8 - 1 = 511 free units, sentinel at FF8.
	 * A 16-byte request takes 3 units: header FE0, payload FE8. */
	init_heap(0x80100000, 0x1000);
	alloc(0, 0);
	alloc(0x1000, 0);
	void *a = alloc(0x10, 0x80100FE8);
	void *b = alloc(1, 0x80100FD8); /* 2 units, header FD0 */
	void *c = alloc(9, 0x80100FC0); /* 3 units, header FB8 */
	void *d = alloc(8, 0x80100FB0); /* 2 units, header FA8 */
	memset(a, 0xA5, 16);
	memset(d, 0x5A, 8);
	release(b); /* isolated free block */
	b = alloc(8, 0x80100FD8); /* exact fit, unlinks that block */
	release(b);
	release(c); /* merges only the upper neighbor: 5 units at FB8 */
	c = alloc(32, 0x80100FC0);
	for (int i = 0; i < 16; i++)
		CHECK(((unsigned char *)a)[i] == 0xA5);
	for (int i = 0; i < 8; i++)
		CHECK(((unsigned char *)d)[i] == 0x5A);
	release(c);
	release(a); /* lower neighbor merges, sentinel remains separate */
	release(d); /* coalesces both neighbors, restoring all 511 units */
	a = alloc(0xFF0, 0x80100008); /* consumes the entire free block */
	alloc(1, 0); /* sentinel-only list terminates */
	release(a); /* insertion across the circular-list boundary */
	a = alloc(0x10, 0x80100FE8);
	release(a);
	alloc(0xFF0, 0x80100008);

	/* InitHeap3 rounds down, and zero address/size leave the previous heap intact. */
	init_heap(0x80100000, 0x1007);
	init_heap(0, 0x1000);
	init_heap(0x80100000, 0);
	alloc(16, 0x80100FE8);
	psyq_libapi_reset();
	init_heap(0x80102000, 0x100); /* reset and reinitialize in a different region */
	alloc(16, 0x801020E8);
	init_heap(0x80102000, 8); /* only a sentinel, no allocatable units */
	alloc(1, 0);

	/* Valid RAM aliases and the actual initializeHeap base all map into the arena. */
	init_heap(0x00100000, 0x1000);
	alloc(16, 0x80100FE8);
	init_heap(0xA0100000, 0x1000);
	alloc(16, 0x80100FE8);
	init_heap(0x801BFB70, 0x1000);
	alloc(16, 0x801C0B58);
}

static int invalid_heap_aborts(uint32_t addr)
{
	pid_t pid = fork();
	if (pid < 0) {
		perror("fork");
		exit(2);
	}
	if (pid == 0) {
		if (setrlimit(RLIMIT_CORE, &(struct rlimit){ 0, 0 }) != 0)
			_exit(2);
		freopen("/dev/null", "w", stderr);
		InitHeap3((unsigned long *)(uintptr_t)addr, 0x1000);
		_exit(0);
	}
	int status;
	if (waitpid(pid, &status, 0) != pid)
		return 0;
	return WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
}

static int callback_count;
static void callback(void) { callback_count++; }

extern void port_pad_latch(uint16_t wire_buttons);

static void pad_state(void)
{
	psyq_libapi_reset();
	clock_reset();
	psyq_libetc_reset();
	CHECK(PadRead(0) == 0xFFFFFFFFu); /* .bss state before PadInit */
	CHECK(clock_now() == CLOCK_PSYQ_ENTRY_CYCLES);
	uint64_t before = clock_now();
	PadInit(0);
	CHECK(clock_now() == before + CLOCK_PSYQ_ENTRY_CYCLES + CLOCK_IRQ_SETUP_CYCLES + CLOCK_PAD_INIT_CPU_CYCLES +
          clock_vblanks() * CLOCK_VBLANK_NOPAD_CPU_CYCLES);
	before = clock_now();
	CHECK(PadRead(0) == 0);
	CHECK(clock_now() == before + CLOCK_PSYQ_ENTRY_CYCLES);
	for (unsigned bit = 0; bit < 16; bit++) {
        port_pad_latch((uint16_t)(1u << bit));
        CHECK(PadRead(0) == (1u << (bit ^ 8)));
    }
    port_pad_latch(0);
    CHECK(PadRead(1) == 0);
	CHECK(PadRead(-1) == 0);
	CHECK(ResetCallback() == 0); /* PadInit already started interrupts */
	VSyncCallback(callback);
	unsigned vcount = Vcount;
	VSync(0);
	CHECK(Vcount == (int)vcount + 1 && callback_count == 1);
	PadInit(1); /* subsequent initialization preserves the callback environment */
	CHECK(PadRead(0) == 0);
	VSync(0);
	CHECK(Vcount == (int)vcount + 2 && callback_count == 2);
	psyq_libetc_reset();
	CHECK(PadRead(0) == 0xFFFFFFFFu);
	PadInit(0);
	CHECK(PadRead(0) == 0);
	vcount = Vcount;
	VSync(0);
	CHECK(Vcount == (int)vcount + 1 && callback_count == 2); /* first ResetCallback clears old callbacks */
}

int main(void)
{
	clock_reset();
	bios_libc_costs();
	clock_reset();
	random_sequences();
	heap_addresses();
	CHECK(invalid_heap_aborts(0x80200000));
	CHECK(invalid_heap_aborts(0x1F800000));
	CHECK(invalid_heap_aborts(0xC0100000));
	pad_state();
	printf("test_libc_heap_pad: %s\n", failures ? "FAIL" : "OK");
	return failures != 0;
}
