/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Pilot-wire heater control over Bluetooth LE: one custom GATT service with
 * a Mode characteristic (enum pw_mode as uint8), read / write / notify.
 * Writes need an encrypted (bonded) link. Just Works pairing is accepted
 * for 2 minutes after boot or after a press of the hidden button (sw1).
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

#include "pilot_wire.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define PAIRING_WINDOW_MS (2 * 60 * 1000)

/* 6f1e0001-5b2c-4d8a-9c3e-2a7b41c0d5e1, Mode is ...0002 */
#define PW_SVC_UUID_VAL BT_UUID_128_ENCODE(0x6f1e0001, 0x5b2c, 0x4d8a, 0x9c3e, 0x2a7b41c0d5e1)
#define PW_MODE_UUID_VAL BT_UUID_128_ENCODE(0x6f1e0002, 0x5b2c, 0x4d8a, 0x9c3e, 0x2a7b41c0d5e1)

static const struct bt_uuid_128 pw_svc_uuid = BT_UUID_INIT_128(PW_SVC_UUID_VAL);
static const struct bt_uuid_128 pw_mode_uuid = BT_UUID_INIT_128(PW_MODE_UUID_VAL);

static uint8_t mode = PW_CONFORT;
static int64_t pairing_until = PAIRING_WINDOW_MS;

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw1), gpios);
static struct gpio_callback button_cb;

static int mode_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	uint8_t val;

	if (strcmp(name, "mode") || len != 1 || read_cb(cb_arg, &val, 1) != 1 ||
	    val >= PW_MODE_COUNT) {
		return -EINVAL;
	}
	mode = val;
	return 0;
}
SETTINGS_STATIC_HANDLER_DEFINE(pw, "pw", NULL, mode_set, NULL, NULL);

static ssize_t read_mode(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			 uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset, &mode, sizeof(mode));
}

static ssize_t write_mode(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			  const void *buf, uint16_t len, uint16_t offset, uint8_t flags);

BT_GATT_SERVICE_DEFINE(pw_svc,
	BT_GATT_PRIMARY_SERVICE(&pw_svc_uuid),
	BT_GATT_CHARACTERISTIC(&pw_mode_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE_ENCRYPT, read_mode, write_mode,
			       NULL),
	BT_GATT_CCC(NULL, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE_ENCRYPT),
);

static ssize_t write_mode(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			  const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	uint8_t val;
	int err;

	if (offset) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}
	if (len != 1) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}
	val = *(const uint8_t *)buf;
	if (val >= PW_MODE_COUNT) {
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}

	mode = val;
	pilot_wire_set(val);
	LOG_INF("mode %u", val);

	err = settings_save_one("pw/mode", &val, 1);
	if (err) {
		LOG_ERR("mode save failed (%d)", err);
	}
	bt_gatt_notify(NULL, &pw_svc.attrs[1], &val, 1);
	return len;
}

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, PW_SVC_UUID_VAL),
};

static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static void start_adv(void)
{
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));

	if (err && err != -EALREADY) {
		LOG_ERR("advertising failed (%d)", err);
	}
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	LOG_INF("connected (%u)", err);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	LOG_INF("disconnected (0x%02x)", reason);
}

static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
	LOG_INF("security level %d (err %d)", level, err);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.recycled = start_adv,
	.security_changed = security_changed,
};

static enum bt_security_err pairing_accept(struct bt_conn *conn,
					   const struct bt_conn_pairing_feat *const feat)
{
	if (k_uptime_get() > pairing_until) {
		LOG_WRN("pairing rejected: press the pairing button first");
		return BT_SECURITY_ERR_PAIR_NOT_ALLOWED;
	}
	return BT_SECURITY_ERR_SUCCESS;
}

static struct bt_conn_auth_cb auth_cb = {
	.pairing_accept = pairing_accept,
};

static void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	pairing_until = k_uptime_get() + PAIRING_WINDOW_MS;
	LOG_INF("pairing open for %d s", PAIRING_WINDOW_MS / 1000);
}

int main(void)
{
	int err;

	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("Bluetooth init failed (%d)", err);
		return 0;
	}
	/* Bonds, identity and the saved mode */
	settings_load();

	err = pilot_wire_init(mode);
	if (err) {
		LOG_ERR("pilot wire init failed (%d)", err);
		return 0;
	}
	LOG_INF("mode %u", mode);

	bt_conn_auth_cb_register(&auth_cb);

	if (gpio_pin_configure_dt(&button, GPIO_INPUT) ||
	    gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE)) {
		LOG_ERR("pairing button unavailable");
	} else {
		gpio_init_callback(&button_cb, button_pressed, BIT(button.pin));
		gpio_add_callback_dt(&button, &button_cb);
	}

	start_adv();
	LOG_INF("advertising as %s", CONFIG_BT_DEVICE_NAME);
	return 0;
}
