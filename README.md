# Intuitiv

Zephyr (v4.5.0) support for the Intuitiv pilot-wire heater gateway, a
board built around an STM32F412RG (LQFP64).

Project status: nothing tested yet, skeleton project only.

 **The board ground is tied to the mains (connector P3, probably the phase).**
The supply is non-isolated. Never connect a debugger, USB, serial adapter or scope ground
while the board is plugged into the mains. Program it powered from 3.3 V only.

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
| Crystal "NSK J 26TAG" | STM32 HSE, **assumed 26 MHz** |

## STM32 pin map

| Pin | Function | Notes |
| --- | --- | --- |
| PC10 | RGB LED channel A, active low | `led0`, used by blinky. Colour unknown |
| PC11 | RGB LED channel B, active low | `led1`. Colour unknown |
| PA15 | RGB LED channel C, active low | `led2`. Colour unknown. JTDI at reset, free as GPIO with SWD only |
| PB10 + PB14 | Optotriac LED anode (via 200 Ω) | Both drive the anode together |
| PC12 | Optotriac LED cathode switch (K38 FN, SOT-23) | Assumed active high, to verify |
| PD2 | Zero-cross detection (EXTI2) | Large-ratio, clipped divider on neutral |
| PB1 | Mains voltage / polarity, ADC1_IN9 | 3× 300 kΩ / 2 kΩ divider |
| PC4 | Pilot-wire output feedback, ADC1_IN14 | 3× 16 kΩ / 2 kΩ divider |
| PA13 | SWDIO | |
| PA14 | SWCLK | |
| NRST | Reset | RC (cap + 10 kΩ to 3V3). Connect under reset |
| PH0 / PH1 | HSE crystal | |

The optotriac LED only lights when PB10/PB14 are high **and** PC12 turns the cathode switch on.
Two conditions protect against a single stuck GPIO.

The LQFP64 pin numbers of PB10, PB14, PC12, PD2, PB1 and PC4 still need rechecking.

## Mains connector

| Pin | Role |
| --- | --- |
| P1 | Neutral (inferred) |
| P2 | Pilot wire output |
| P3 | Phase (inferred) = board GND |

## Pilot-wire orders

Voltage measured between P2 and neutral. The optotriac fires at the next zero crossing after its
LED turns on, and the triac turns off at the next zero crossing after it turns off. So toggling
the LED once per half-cycle (synchronised on PD2, polarity from PB1) selects half-waves.

| Order | Signal | Firmware |
| --- | --- | --- |
| Confort | 0 V | LED off |
| Éco | Full 230 V | LED on continuously |
| Hors-gel | Negative half-wave | LED on during negative half-waves only |
| Arrêt | Positive half-wave | LED on during positive half-waves only |
| Confort −1 / −2 | 230 V for 3 s / 7 s every 5 min | Timed full-wave pulses |

PC4 lets firmware check what actually came out.

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
west flash   # stm32cubeprogrammer over SWD (default), or --runner openocd
```

No console is configured yet: the UART pins are not identified.
