/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Wi-Fi firmware for WHD (CONFIG_AIROC_WIFI_CUSTOM, RESOURCE_READ_FROM_C_FILE).
 * The 43430 A0 firmware has its CLM built in, so the CLM blob is empty and
 * WHD skips the download.
 */

#include "wiced_resource.h"

const unsigned char wifi_firmware_image_data[] __aligned(4) = {
#include "wifi_fw.inc"
};

const resource_hnd_t wifi_firmware_image = {RESOURCE_IN_MEMORY,
					    sizeof(wifi_firmware_image_data),
					    {.mem = {(const char *)wifi_firmware_image_data}}};

const unsigned char wifi_firmware_clm_blob_data[1];

const resource_hnd_t wifi_firmware_clm_blob = {
	RESOURCE_IN_MEMORY, 0, {.mem = {(const char *)wifi_firmware_clm_blob_data}}};
