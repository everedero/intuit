/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Pilot-wire output. The MOC3063 fires at the next zero crossing after its
 * LED turns on, and the triac stops at the next crossing after the LED turns
 * off. So the LED is set mid half-wave to "is the next half-wave wanted".
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "pilot_wire.h"

LOG_MODULE_REGISTER(pilot_wire, LOG_LEVEL_INF);

#define ZC_DELAY_MS 5       /* mid half-wave at 50 Hz */
#define ZC_LOST_MS 40       /* two full mains periods */
#define TICK_MS 100
#define STATS_TICKS 100     /* 10 s */

static const struct gpio_dt_spec opto = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), opto_gpios);

static atomic_t cur_mode;
static int64_t mode_start;
static int64_t last_zc;
static enum pw_half cur_half;
static atomic_t count[2];

static uint32_t cycle_ms(void)
{
	return (uint32_t)((k_uptime_get() - mode_start) % PW_CYCLE_MS);
}

/* Mid half-wave: arm (or not) the optotriac for the next one */
static void mid_half(struct k_timer *t)
{
	enum pw_half next = cur_half == PW_HALF_POS ? PW_HALF_NEG : PW_HALF_POS;
	bool on = pw_conduct(atomic_get(&cur_mode), next, cycle_ms());

	gpio_pin_set_dt(&opto, on);
	if (on) {
		atomic_inc(&count[next]);
	}
}
static K_TIMER_DEFINE(mid_timer, mid_half, NULL);

static void zero_cross(enum pw_half starting)
{
	if (IS_ENABLED(CONFIG_PILOT_WIRE_INVERT_POLARITY)) {
		starting = starting == PW_HALF_POS ? PW_HALF_NEG : PW_HALF_POS;
	}
	cur_half = starting;
	last_zc = k_uptime_get();
	k_timer_start(&mid_timer, K_MSEC(ZC_DELAY_MS), K_NO_WAIT);
}

#ifdef CONFIG_PILOT_WIRE_SIM_ZC
static void sim_zc(struct k_timer *t)
{
	static enum pw_half half;

	half = half == PW_HALF_POS ? PW_HALF_NEG : PW_HALF_POS;
	zero_cross(half);
}
static K_TIMER_DEFINE(sim_timer, sim_zc, NULL);
#else
static const struct gpio_dt_spec zc = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), zero_cross_gpios);
static struct gpio_callback zc_cb;

static void zc_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	zero_cross(gpio_pin_get_dt(&zc) ? PW_HALF_POS : PW_HALF_NEG);
}
#endif

/* No mains (bench): only full-wave orders can be expressed, as LED on */
static void tick(struct k_work *work)
{
	static int ticks;

	if (k_uptime_get() - last_zc > ZC_LOST_MS) {
		enum pw_mode mode = atomic_get(&cur_mode);
		uint32_t t = cycle_ms();

		gpio_pin_set_dt(&opto, pw_conduct(mode, PW_HALF_POS, t) &&
					       pw_conduct(mode, PW_HALF_NEG, t));
	}

	if (++ticks == STATS_TICKS) {
		ticks = 0;
		LOG_INF("mode %ld, half-waves on: +%ld -%ld", atomic_get(&cur_mode),
			atomic_set(&count[PW_HALF_POS], 0), atomic_set(&count[PW_HALF_NEG], 0));
	}
	k_work_schedule(k_work_delayable_from_work(work), K_MSEC(TICK_MS));
}
static K_WORK_DELAYABLE_DEFINE(tick_work, tick);

void pilot_wire_set(enum pw_mode mode)
{
	mode_start = k_uptime_get();
	atomic_set(&cur_mode, mode);
}

int pilot_wire_init(enum pw_mode mode)
{
	int err;

	pilot_wire_set(mode);
	last_zc = INT64_MIN / 2;

	err = gpio_pin_configure_dt(&opto, GPIO_OUTPUT_INACTIVE);
	if (err) {
		return err;
	}

#ifdef CONFIG_PILOT_WIRE_SIM_ZC
	LOG_WRN("simulated zero-cross");
	k_timer_start(&sim_timer, K_MSEC(10), K_MSEC(10));
#else
	err = gpio_pin_configure_dt(&zc, GPIO_INPUT);
	if (err) {
		return err;
	}
	gpio_init_callback(&zc_cb, zc_isr, BIT(zc.pin));
	err = gpio_add_callback_dt(&zc, &zc_cb);
	if (err) {
		return err;
	}
	err = gpio_pin_interrupt_configure_dt(&zc, GPIO_INT_EDGE_BOTH);
	if (err) {
		return err;
	}
#endif

	k_work_schedule(&tick_work, K_NO_WAIT);
	return 0;
}
