/*
 * Port replacement of include/__rts_info_t__.h (AD-4). CodeWarrior's linker stores these values
 * as data words; the game reads _end, _stack_addr and _stack_size to size its heap
 * (initializeHeap). The port renames them, so they cannot clash with the host linker's _end,
 * and gives them the PS1 values (port/psyq/crt.c, checked against SLUS_010.32). initializeHeap
 * hands InitHeap3 the raw PS1 address 0x801BFB70: the port's InitHeap3 (story 2.9) maps it into
 * the arena with PS1_RAM.
 */
#ifndef PORT_RTS_INFO_T_H
#define PORT_RTS_INFO_T_H

#define _end dw1_rts_end
#define _stack_addr dw1_rts_stack_addr
#define _stack_size dw1_rts_stack_size

extern unsigned long _end;
extern unsigned long _stack_addr;
extern unsigned long _stack_size;

#endif
