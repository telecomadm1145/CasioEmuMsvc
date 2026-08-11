/* Portable core diagnostic output hook. */
#include "diag_internal.h"

#include <stdarg.h>
#include <stdio.h>

enum {
	CORE_DIAG_BUFFER_SIZE = 256
};

void core_diag_set_writer_state(struct core_diag_state *state, core_diag_write_fn write_fn, void *user) {
	if (!state) {
		return;
	}

	state->write_fn = write_fn;
	state->write_user = user;
}

void core_diag_printf_state(struct core_diag_state *state, const char *fmt, ...) {
	char buf[CORE_DIAG_BUFFER_SIZE];
	va_list args;
	int len;

	if (!state || !state->write_fn || !fmt) {
		return;
	}

	va_start(args, fmt);
	len = vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	if (len <= 0) {
		return;
	}
	if ((size_t)len >= sizeof(buf)) {
		len = (int)sizeof(buf) - 1;
	}
	state->write_fn(buf, (size_t)len, state->write_user);
}


