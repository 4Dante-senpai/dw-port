/* Port trace and the logging of the generated PsyQ stubs (tools/port/psyq/gen_stubs.py). */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "clock.h"
#include "port.h"

static FILE *trace;

void port_trace(const char *fmt, ...)
{
	if (!trace) {
		const char *path = getenv("DW1_TRACE");
		trace = fopen(path ? path : "port_trace.log", "w");
		if (!trace) {
			fprintf(stderr, "port: cannot open the trace %s\n", path ? path : "port_trace.log");
			return;
		}
		setvbuf(trace, NULL, _IOLBF, 0);
	}
	va_list ap;
	va_start(ap, fmt);
	fprintf(trace, "%llu ", (unsigned long long)clock_now());
	vfprintf(trace, fmt, ap);
	fputc('\n', trace);
	va_end(ap);
}

/* First call of each stub: one line "<cycle> stub <name>" in the trace. */
void port_stub_hit(const char *name, int *seen)
{
	clock_psyq_entry();
	if (*seen)
		return;
	*seen = 1;
	port_trace("stub %s", name);
}
