/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * RGB LED and buttons. On native_sim the emulated GPIO controller lets the
 * test press the buttons and see the LED pin levels; on the board nobody
 * presses them, so they must read released.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/ztest.h>

#ifdef CONFIG_GPIO_EMUL
#include <zephyr/drivers/gpio/gpio_emul.h>
#endif

static const struct gpio_dt_spec leds[] = {
	GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios),
	GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios),
	GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios),
};

static const struct gpio_dt_spec buttons[] = {
	GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios),
	GPIO_DT_SPEC_GET(DT_ALIAS(sw1), gpios),
};

ZTEST(board_leds, test_set_and_read_back)
{
	ARRAY_FOR_EACH(leds, i) {
		const struct gpio_dt_spec *led = &leds[i];

		zassert_true(gpio_is_ready_dt(led));
		zassert_ok(gpio_pin_configure_dt(led, GPIO_OUTPUT_INACTIVE | GPIO_INPUT));
		for (int on = 1; on >= 0; on--) {
			zassert_ok(gpio_pin_set_dt(led, on));
			zassert_equal(gpio_pin_get_dt(led), on, "led%zu", i);
#ifdef CONFIG_GPIO_EMUL
			/* Active low: lit means the pin is driven low */
			zassert_equal(gpio_emul_output_get(led->port, led->pin), !on);
#endif
		}
	}
}

ZTEST_SUITE(board_leds, NULL, NULL, NULL, NULL, NULL);

static K_SEM_DEFINE(pressed, 0, 1);

static void on_press(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	k_sem_give(&pressed);
}

ZTEST(board_buttons, test_released_and_irq)
{
	ARRAY_FOR_EACH(buttons, i) {
		const struct gpio_dt_spec *btn = &buttons[i];
		struct gpio_callback cb;

		zassert_true(gpio_is_ready_dt(btn));
		zassert_ok(gpio_pin_configure_dt(btn, GPIO_INPUT));
		zassert_equal(gpio_pin_get_dt(btn), 0, "sw%zu reads pressed", i);

		gpio_init_callback(&cb, on_press, BIT(btn->pin));
		zassert_ok(gpio_add_callback_dt(btn, &cb));
		zassert_ok(gpio_pin_interrupt_configure_dt(btn, GPIO_INT_EDGE_TO_ACTIVE));
		k_sem_reset(&pressed);
#ifdef CONFIG_GPIO_EMUL
		zassert_ok(gpio_emul_input_set(btn->port, btn->pin, 1));
		zassert_ok(k_sem_take(&pressed, K_MSEC(100)), "sw%zu: no interrupt", i);
		zassert_equal(gpio_pin_get_dt(btn), 1);
		zassert_ok(gpio_emul_input_set(btn->port, btn->pin, 0));
#else
		/* Released: no edge, so no interrupt */
		zassert_not_equal(k_sem_take(&pressed, K_MSEC(100)), 0, "sw%zu bounced", i);
#endif
		zassert_ok(gpio_pin_interrupt_configure_dt(btn, GPIO_INT_DISABLE));
		zassert_ok(gpio_remove_callback_dt(btn, &cb));
	}
}

ZTEST_SUITE(board_buttons, NULL, NULL, NULL, NULL, NULL);
