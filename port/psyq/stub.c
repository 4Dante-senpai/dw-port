/* Logging of the generated PsyQ stubs (tools/port/psyq/gen_stubs.py). */
#include <stdio.h>
#include <stdlib.h>

#include "clock.h"

static FILE *trace;

/* First call of each stub: one line "<cycle> stub <name>" in $DW1_TRACE (default port_trace.log). */
void port_stub_hit(const char *name, int *seen)
{
	clock_psyq_entry();
	if (*seen)
		return;
	*seen = 1;
	if (!trace) {
		const char *path = getenv("DW1_TRACE");
		trace = fopen(path ? path : "port_trace.log", "w");
		if (!trace) {
			fprintf(stderr, "port: cannot open the stub trace %s\n", path ? path : "port_trace.log");
			return;
		}
		setvbuf(trace, NULL, _IOLBF, 0);
	}
	fprintf(trace, "%llu stub %s\n", (unsigned long long)clock_now(), name);
}
