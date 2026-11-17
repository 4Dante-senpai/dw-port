/* Observable game state for the live playtest channel; reads globals only. */
#ifndef PORT_STATE_H
#define PORT_STATE_H
#include <stdio.h>
void port_state_header(FILE *out);
void port_state_record(FILE *out, unsigned k, int want);
#endif
