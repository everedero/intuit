# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources_ifdef(CONFIG_SDHC_STM32F4_SDIO sdhc_stm32f4_sdio.c)
