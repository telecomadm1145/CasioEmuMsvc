/* Internal timer state and API. */
#ifndef FX_EMU_CORE_TIMER_INTERNAL_H
#define FX_EMU_CORE_TIMER_INTERNAL_H

#include <stdint.h>

enum { TIMER_REG_COUNT = 0x30 };

struct cpu_state;
struct mmio_state;

struct timer_state {
	uint8_t reg[TIMER_REG_COUNT]; /* Many are not used. */
	struct cpu_state *cpu;
	struct mmio_state *mmio;
	int32_t t0psc, t1psc, t2psc; /* Prescaler counter */
	int32_t t0prl, t1prl, t2prl; /* Prescaler reload */
	int32_t t0cnt, t1cnt, t2cnt; /* Counter */
	int32_t t0crl, t1crl, t2crl; /* Counter reload */
};

void timer_connect_bus_state(struct timer_state *state, struct cpu_state *cpu, struct mmio_state *mmio);
uint8_t timer_read_byte_state(struct timer_state *state, uint8_t addr);
void timer_write_byte_state(struct timer_state *state, uint8_t addr, uint8_t byte);
void timer_tick_state(struct timer_state *state, uint32_t cycles);
void timer_reset_state(struct timer_state *state);

#endif /* FX_EMU_CORE_TIMER_INTERNAL_H */


