/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * AP6212 Bluetooth: patchram download (bt_enable), public identity address,
 * connectable advertising start and stop.
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/ztest.h>

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
};

static void *bt_setup(void)
{
	zassert_ok(bt_enable(NULL));
	return NULL;
}

ZTEST(board_bt, test_identity)
{
	bt_addr_le_t addr;
	size_t count = 1;

	bt_id_get(&addr, &count);
	zassert_equal(count, 1);
	zassert_false(bt_addr_le_eq(&addr, BT_ADDR_LE_ANY));
	TC_PRINT("identity type %u\n", addr.type);
}

ZTEST(board_bt, test_advertise)
{
	zassert_ok(bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), NULL, 0));
	k_sleep(K_MSEC(500));
	zassert_ok(bt_le_adv_stop());
}

ZTEST_SUITE(board_bt, NULL, bt_setup, NULL, NULL, NULL);
