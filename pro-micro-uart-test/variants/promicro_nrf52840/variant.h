/*
 * Minimal local variant for the custom promicro_nrf52840 board.
 * Mirrors the pin map used in MeshCore's Pro Micro variant so this
 * standalone test project builds independently.
 */

#pragma once

#include "WVariant.h"

#define VARIANT_MCK       (64000000ul)
#define USE_LFRC

#define PIN_EXT_VCC          (21)
#define EXT_VCC              (PIN_EXT_VCC)

#define BATTERY_PIN          (17)
#define ADC_RESOLUTION       12

#define PINS_COUNT           (23)
#define NUM_DIGITAL_PINS     (23)
#define NUM_ANALOG_INPUTS    (3)
#define NUM_ANALOG_OUTPUTS   (0)

#define PIN_SERIAL1_TX       (1)
#define PIN_SERIAL1_RX       (0)

#define WIRE_INTERFACES_COUNT 2

#define PIN_WIRE_SDA         (6)
#define PIN_WIRE_SCL         (7)
#define PIN_WIRE1_SDA        (13)
#define PIN_WIRE1_SCL        (14)

#define SPI_INTERFACES_COUNT 2

#define PIN_SPI_SCK          (2)
#define PIN_SPI_MISO         (3)
#define PIN_SPI_MOSI         (4)
#define PIN_SPI_NSS          (5)

#define PIN_SPI1_SCK         (18)
#define PIN_SPI1_MISO        (19)
#define PIN_SPI1_MOSI        (20)

#define PIN_LED              (22)
#define LED_PIN              PIN_LED
#define LED_BLUE             PIN_LED
#define LED_BUILTIN          PIN_LED
#define LED_STATE_ON         1

#define PIN_BUTTON1          (6)
#define BUTTON_PIN           PIN_BUTTON1

