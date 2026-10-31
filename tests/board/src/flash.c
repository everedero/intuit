/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * SPI NOR: geometry, JEDEC id, and an erase/write/read cycle on one sector
 * that the factory image leaves erased (see README, SPI flash dump).
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/ztest.h>

#define FLASH_NODE DT_NODELABEL(spi_flash)

/* native_sim: the flash simulator device is the parent controller */
#if DT_NODE_HAS_COMPAT(FLASH_NODE, soc_nv_flash)
#define FLASH_DEV_NODE DT_PARENT(FLASH_NODE)
#define FLASH_SIZE DT_REG_SIZE(FLASH_NODE)
#else
#define FLASH_DEV_NODE FLASH_NODE
#define FLASH_SIZE (DT_PROP(FLASH_NODE, size) / 8)
#endif

/* Erased in the factory dump: data ends at 0x53ffff, resumes at 0x7e0000 */
#define TEST_OFFSET 0x700000
#define SECTOR 4096

static const struct device *const flash = DEVICE_DT_GET(FLASH_DEV_NODE);
static uint8_t buf[SECTOR];

ZTEST(board_flash, test_geometry)
{
	struct flash_pages_info info;

	zassert_true(device_is_ready(flash));
	zassert_equal(FLASH_SIZE, 8 * 1024 * 1024);
	zassert_ok(flash_get_page_info_by_offs(flash, TEST_OFFSET, &info));
	zassert_equal(info.size, SECTOR);
	zassert_equal(info.start_offset, TEST_OFFSET);
}

ZTEST(board_flash, test_jedec_id)
{
#if DT_NODE_HAS_PROP(FLASH_NODE, jedec_id)
	uint8_t id[3];
	const uint8_t want[] = DT_PROP(FLASH_NODE, jedec_id);

	zassert_ok(flash_read_jedec_id(flash, id));
	zassert_mem_equal(id, want, sizeof(id));
#else
	ztest_test_skip();
#endif
}

ZTEST(board_flash, test_erase_write_read)
{
	zassert_ok(flash_erase(flash, TEST_OFFSET, SECTOR));
	zassert_ok(flash_read(flash, TEST_OFFSET, buf, SECTOR));
	for (size_t i = 0; i < SECTOR; i++) {
		zassert_equal(buf[i], 0xff, "not erased at %zu", i);
	}

	for (size_t i = 0; i < SECTOR; i++) {
		buf[i] = (uint8_t)(i * 7 + 3);
	}
	zassert_ok(flash_write(flash, TEST_OFFSET, buf, SECTOR));
	memset(buf, 0, SECTOR);
	zassert_ok(flash_read(flash, TEST_OFFSET, buf, SECTOR));
	for (size_t i = 0; i < SECTOR; i++) {
		zassert_equal(buf[i], (uint8_t)(i * 7 + 3), "bad byte at %zu", i);
	}

	/* Leave the sector erased, as found */
	zassert_ok(flash_erase(flash, TEST_OFFSET, SECTOR));
}

ZTEST_SUITE(board_flash, NULL, NULL, NULL, NULL, NULL);
