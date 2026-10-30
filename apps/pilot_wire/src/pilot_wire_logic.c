/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pilot_wire_logic.h"

bool pw_conduct(enum pw_mode mode, enum pw_half half, uint32_t t_ms)
{
	switch (mode) {
	case PW_ECO:
		return true;
	case PW_HORS_GEL:
		return half == PW_HALF_NEG;
	case PW_ARRET:
		return half == PW_HALF_POS;
	case PW_CONFORT_M1:
		return t_ms % PW_CYCLE_MS < 3000U;
	case PW_CONFORT_M2:
		return t_ms % PW_CYCLE_MS < 7000U;
	default:
		return false;
	}
}
