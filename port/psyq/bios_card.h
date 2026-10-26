#ifndef DW1_BIOS_CARD_H
#define DW1_BIOS_CARD_H
#include <stdint.h>
#define CARD_SW 0xf4000001u
#define CARD_HW 0xf0000011u
typedef struct BiosDir {
	char name[20];
	long attr, size;
	struct BiosDir *next;
	long head;
	char system[4];
} BiosDir;
long OpenEvent(unsigned long, unsigned long, long, long (*)(void));
long CloseEvent(long);
long EnableEvent(long);
long DisableEvent(long);
long TestEvent(long);
long EnterCriticalSection(void);
void ExitCriticalSection(void);
void bios_card_deliver(uint32_t, uint32_t);
void bios_card_init(void);
uint32_t bios_card_vblank_cpu_cycles(void);
void bios_card_stop(void);
int bios_card_info(int);
int bios_card_load(int);
int bios_card_write(int, int, const uint8_t *);
void bios_card_new(void);
void bios_bu_init(void);
int bios_bu_open(const char *, unsigned);
int bios_bu_close(int);
int bios_bu_seek(int, int);
int bios_bu_io(int, void *, int, int);
int bios_bu_erase(const char *);
int bios_bu_format(int);
BiosDir *bios_bu_first(const char *, BiosDir *);
BiosDir *bios_bu_next(BiosDir *);
#endif
