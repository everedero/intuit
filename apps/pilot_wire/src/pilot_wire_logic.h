/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Pilot-wire ("fil pilote") orders, as seen on the output (P2 against neutral).
 */

#ifndef PILOT_WIRE_LOGIC_H_
#define PILOT_WIRE_LOGIC_H_

#include <stdbool.h>
#include <stdint.h>

/* Values of the Bluetooth Mode characteristic: keep them stable */
enum pw_mode {
	PW_CONFORT = 0,    /* 0 V */
	PW_ECO = 1,        /* full wave */
	PW_HORS_GEL = 2,   /* negative half-waves */
	PW_ARRET = 3,      /* positive half-waves */
	PW_CONFORT_M1 = 4, /* full wave 3 s every 300 s */
	PW_CONFORT_M2 = 5, /* full wave 7 s every 300 s */
	PW_MODE_COUNT,
};

enum pw_half {
	PW_HALF_POS,
	PW_HALF_NEG,
};

#define PW_CYCLE_MS 300000U

/*
 * Whether the triac conducts during `half`, `t_ms` milliseconds into the
 * 300 s Confort -1/-2 cycle.
 */
bool pw_conduct(enum pw_mode mode, enum pw_half half, uint32_t t_ms);

#endif /* PILOT_WIRE_LOGIC_H_ */
