/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Dump the whole SPI flash on the console UART (TX only), forever. Each
 * 4 KiB block goes out as "BLK" + address (LE32) + data + CRC32 (LE32) of
 * address and data, so the host keeps the good blocks across passes.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>

#define FLASH_NODE DT_NODELABEL(spi_flash)
#define FLASH_SIZE (DT_PROP(FLASH_NODE, size) / 8)
#define BLK_SIZE   4096

static const struct device *const flash = DEVICE_DT_GET(FLASH_NODE);
static const struct device *const uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
static uint8_t blk[BLK_SIZE];

static void out(const uint8_t *p, size_t n)
{
	while (n--) {
		uart_poll_out(uart, *p++);
	}
}

int main(void)
{
	uint8_t hdr[7] = {'B', 'L', 'K'};
	uint8_t crc[4];

	if (!device_is_ready(flash) || !device_is_ready(uart)) {
		return 0;
	}

	for (;;) {
		for (uint32_t addr = 0; addr < FLASH_SIZE; addr += BLK_SIZE) {
			if (flash_read(flash, addr, blk, BLK_SIZE)) {
				continue; /* host sees the block missing */
			}
			sys_put_le32(addr, &hdr[3]);
			sys_put_le32(crc32_ieee_update(crc32_ieee(&hdr[3], 4), blk, BLK_SIZE), crc);
			out(hdr, sizeof(hdr));
			out(blk, BLK_SIZE);
			out(crc, sizeof(crc));
		}
		k_msleep(1000);
	}
	return 0;
}
