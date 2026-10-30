/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include "pilot_wire_logic.h"

ZTEST_SUITE(pilot_wire_logic, NULL, NULL, NULL, NULL, NULL);

ZTEST(pilot_wire_logic, test_static_orders)
{
	zassert_false(pw_conduct(PW_CONFORT, PW_HALF_POS, 0));
	zassert_false(pw_conduct(PW_CONFORT, PW_HALF_NEG, 0));
	zassert_true(pw_conduct(PW_ECO, PW_HALF_POS, 0));
	zassert_true(pw_conduct(PW_ECO, PW_HALF_NEG, 0));
}

ZTEST(pilot_wire_logic, test_half_wave_orders)
{
	zassert_false(pw_conduct(PW_HORS_GEL, PW_HALF_POS, 0));
	zassert_true(pw_conduct(PW_HORS_GEL, PW_HALF_NEG, 0));
	zassert_true(pw_conduct(PW_ARRET, PW_HALF_POS, 0));
	zassert_false(pw_conduct(PW_ARRET, PW_HALF_NEG, 0));
}

ZTEST(pilot_wire_logic, test_comfort_minus_pulses)
{
	/* -1: 3 s of full wave at the start of each 300 s cycle */
	zassert_true(pw_conduct(PW_CONFORT_M1, PW_HALF_POS, 0));
	zassert_true(pw_conduct(PW_CONFORT_M1, PW_HALF_NEG, 2999));
	zassert_false(pw_conduct(PW_CONFORT_M1, PW_HALF_POS, 3000));
	zassert_false(pw_conduct(PW_CONFORT_M1, PW_HALF_POS, PW_CYCLE_MS - 1));
	zassert_true(pw_conduct(PW_CONFORT_M1, PW_HALF_POS, PW_CYCLE_MS + 10));
	/* -2: 7 s */
	zassert_true(pw_conduct(PW_CONFORT_M2, PW_HALF_NEG, 6999));
	zassert_false(pw_conduct(PW_CONFORT_M2, PW_HALF_NEG, 7000));
}

ZTEST(pilot_wire_logic, test_unknown_mode_is_confort)
{
	zassert_false(pw_conduct(PW_MODE_COUNT, PW_HALF_POS, 0));
	zassert_false(pw_conduct((enum pw_mode)200, PW_HALF_NEG, 0));
}
