/* Internal diagnostic state and API */
#ifndef FX_EMU_CORE_DIAG_INTERNAL_H
#define FX_EMU_CORE_DIAG_INTERNAL_H

#include <stddef.h>

typedef void (*core_diag_write_fn)(const char *data, size_t size, void *user);

struct core_diag_state {
	core_diag_write_fn write_fn;
	void *write_user;
};

void core_diag_set_writer_state(struct core_diag_state *state, core_diag_write_fn write_fn, void *user);
void core_diag_printf_state(struct core_diag_state *state, const char *fmt, ...);

#endif /* FX_EMU_CORE_DIAG_INTERNAL_H */


