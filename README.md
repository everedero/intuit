# Intuitiv

Zephyr (v4.5.0) support for the Intuitiv pilot-wire heater gateway, a
board built around an STM32F412RG (LQFP64).

Project status: blinky runs (console at 2 Mbaud on PB6 confirms the 26 MHz HSE), and the
`samples/drivers/spi_flash` test passes on the AT25SF641. Stock firmware erased (it was RDP
level 1, now RDP 0).

 **The board ground is tied to the mains (connector P3, probably the phase).**
The supply is non-isolated. Never connect a debugger, USB, serial adapter or scope ground
while the board is plugged into the mains. Program it powered from 3.3 V only.

TX testpoint (original app):
```
screen /dev/ttyACM0 2000000
```

## Hardware

| Part | Function |
| --- | --- |
| STM32F412RG (LQFP64) | Main MCU |
| AP6212 | Wi-Fi + Bluetooth SiP |
| ATSAMR21E18A | 2.4 GHz 802.15.4 radio MCU |
| AT25SF641 | 64 Mbit SPI NOR flash |
| VIPER013LS | Non-isolated offline converter (half-wave rectified mains) |
| NCP1117 | 3.3 V LDO |
| MOC3063M | Zero-cross optotriac (triac driver) |
| T435-600B | 4 A triac, pilot-wire output |
| Crystal "NSK B 26TAG" | STM32 HSE, **assumed 26 MHz** |
| Crystal "NSK B 16TAH" | ATSAMR21 clock, assumed 16 MHz |
| Crystal "NSK J 26TAG" | AP6212 clock, assumed 26 MHz |

## STM32 pin map

| Pin (LQFP64) | Function | Notes |
| --- | --- | --- |
| PB13 (34) | RGB LED red, active low | `led0`, used by blinky. TIM1_CH1N |
| PB14 (35) | RGB LED green, active low | `led1`. TIM1_CH2N |
| PB15 (36) | RGB LED blue, active low | `led2`. TIM1_CH3N |
| VDDA (13) / VDD (19) | Optotriac LED anode (via 200 Ω) | Fed from 3V3 |
| PC6 (37) | Optotriac LED cathode switch (K38 FN, SOT-23, no test point) | Assumed active high. Hogged low in DTS |
| PC7 (38) | Zero-cross detection, TP2 (EXTI7) | Large-ratio, clipped divider on neutral |
| PC3 (11) | Mains voltage / polarity, TP1, ADC1_IN13 | 3× 300 kΩ / 2 kΩ divider |
| PC0 (8) | Pilot-wire output feedback, TP3, ADC1_IN10 | 3× 16 kΩ / 2 kΩ divider |
| PA4 (20) | SPI flash CS | GPIO chip select |
| PA5 (21) | SPI1 SCK, SPI flash | |
| PA6 (22) | SPI1 MISO, SPI flash | |
| PA7 (23) | SPI1 MOSI, SPI flash | |
| PA11 (44) | USB D− (white) | |
| PA12 (45) | USB D+ (green) | |
| PB5 (57) | Middle user button | `sw0`, active high (switch to 3V3), no external pull: internal pull-down |
| PC2 (10) | Hidden button (pairing reset) | `sw1`, active high (switch to 3V3), no external pull: internal pull-down |
| PA15, PC10–PC12, PD2, PB3, PB4 (50–56) | Hardware version resistors | 6 footprints, 3 fitted, exact pins to trace. With pull-ups, PA15 and PB3 read low |
| PB6 (58) | Bottom test pad | USART1_TX: console |
| PA13 (46) | SWDIO | |
| PA14 (49) | SWCLK | |
| NRST (7) | Reset | 280 kΩ pull-up, no test point. Not wired to the probe |
| PH0 / PH1 (5, 6) | HSE crystal | |

The optotriac LED anode sits on 3V3: PC6 alone turns it on through the cathode switch.

All pins except SWD were first read rotated by 90° (16 pins);
the table above has the corrected pins.

## Bottom test pad (unmarked, near the MCU)

```
o o o                              o o o
5V?  10k pull-up to VDD  10k to BOOT0  PB6  3V3  GND
```

BOOT0 has a 100 kΩ pull-down, so wiring the BOOT0 pad to the 3V3 pad forces BOOT0 high.

PB11 also reads low with a pull-up: unknown, not in the version group.

## Console

Two options, both TX + GND, **bench supply only, never on mains**:

1. **USB connector, no soldering**: build with `-S cdc-acm-console`, plug USB, open `/dev/ttyACM0`.
2. **UART on the bottom test pad** (default build): USART1 TX on PB6, **2 000 000 baud 8N1**,
   the same speed as the stock firmware (it prints `[Intuitiv Gateway]` at boot).
   With a Nucleo ST-LINK/V2.1: PB6 to CN3 **RX**, read on `/dev/ttyACM0`.

## SWD access

NRST is not reachable (280 kΩ pull-up, no test point), and the stock firmware disables SWD after
boot. The OpenOCD config uses `reset_config none` (software reset) for that reason.

**While debugging, wire the BOOT0 test pad to the 3V3 pad.** Every reset then starts the ST ROM
bootloader, where SWD always works. `west flash` (openocd runner, the default) resets into the
bootloader, writes the flash, then starts the image from its vector table (`intuitiv_run_app` in
`support/openocd.cfg`): no power-cycle, no manual step. SWD also attaches to a running image.
Remove the wire to boot the image standalone.

### Remove readout protection and flash (first time)

The stock firmware ships with RDP level 1. Going back to level 0 **mass-erases the flash**
(the stock firmware cannot be read out or backed up first).

1. Bench 3.3 V supply, ST-LINK on GND / SWDIO / SWCLK (+ VDD_TARGET on 3V3).
2. Wire the BOOT0 pad to 3V3 and power-cycle.
3. Check the protection level (RDP byte = bits 15:8 of `FLASH_OPTCR`; `0x55` = level 1,
   `0xAA` = level 0):
   ```bash
   openocd -f boards/vendor/intuitiv/support/openocd.cfg \
       -c 'init' -c 'halt' -c 'mdw 0x40023C14' -c 'shutdown'
   ```
4. Set RDP level 0 (mass erase), then power-cycle:
   ```bash
   openocd -f boards/vendor/intuitiv/support/openocd.cfg \
       -c 'init' -c 'halt' -c 'stm32f2x unlock 0' -c 'shutdown'
   ```
   `mdw 0x40023C14` should now read `0x0FFFAAED`.
5. Flash (from `zephyr_workspace`, after the build below):
   ```bash
   ZEPHYR_BASE=$PWD/zephyr west flash
   ```
   Blinky starts and prints `LED state: ON/OFF` at 2 Mbaud on PB6.

Later flashes only need step 5.

## Mains connector

| Pin | Role |
| --- | --- |
| P1 | Neutral (inferred) |
| P2 | Pilot wire output |
| P3 | Phase (inferred) = board GND |

## Pilot-wire orders

Voltage measured between P2 and neutral. The optotriac fires at the next zero crossing after its
LED turns on, and the triac turns off at the next zero crossing after it turns off. So toggling
the LED once per half-cycle (synchronised on PC7, polarity from PC3) selects half-waves.

| Order | Signal | Firmware |
| --- | --- | --- |
| Confort | 0 V | LED off |
| Éco | Full 230 V | LED on continuously |
| Hors-gel | Negative half-wave | LED on during negative half-waves only |
| Arrêt | Positive half-wave | LED on during positive half-waves only |
| Confort −1 / −2 | 230 V for 3 s / 7 s every 5 min | Timed full-wave pulses |

PC0 lets firmware check what actually came out.

## Clock

HSE 26 MHz → PLL (M=13, N=96, P=2, Q=4) → 96 MHz SYSCLK, 48 MHz PLLQ.
The crystal frequency is inferred from its marking. If blinky runs at the wrong rate or not at
all, switch the PLL source to `&clk_hsi` (16 MHz) with `div-m = <8>` in
`boards/vendor/intuitiv/intuitiv.dts`.

## Build and flash

```bash
cd zephyr_workspace
source ~/zephyrproject/.venv/bin/activate   # Zephyr 4.5 needs Python >= 3.12
west config manifest.path intuitiv && west update
# ZEPHYR_BASE must be absolute: a relative one is resolved from the sample dir
ZEPHYR_BASE=$PWD/zephyr west build -p -b intuitiv zephyr/samples/basic/blinky \
    -- -DPython3_EXECUTABLE=$(which python3)
# console over USB instead of the PB6 UART: add -S cdc-acm-console
west flash   # openocd over SWD (default), or --runner stm32cubeprogrammer
```

