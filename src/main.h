#pragma once

#include "driver/gpio.h"

// ESP32-S3 pin assignments from Documents/3d-ir-schematic.pdf.
#define PIN_LED_LEFT GPIO_NUM_1
#define PIN_LED_RIGHT GPIO_NUM_2
#define PIN_LED_STATUS_DISCON GPIO_NUM_3

#define PIN_BOOT_BUTTON GPIO_NUM_0

#define PIN_BANK1 GPIO_NUM_4
#define PIN_BANK2 GPIO_NUM_5
#define PIN_BANK3 GPIO_NUM_6
#define PIN_BANK4 GPIO_NUM_7
#define PIN_BANK5 GPIO_NUM_8
#define PIN_BANK6 GPIO_NUM_9

#define PIN_TRIGGER GPIO_NUM_10

#define PIN_RS485_RXD GPIO_NUM_11
#define PIN_RS485_TXD GPIO_NUM_12
#define PIN_RS485_TXE GPIO_NUM_13

#define PIN_HART1 GPIO_NUM_15
#define PIN_HART2 GPIO_NUM_16

#define PIN_I2C_SDA GPIO_NUM_17
#define PIN_I2C_SCL GPIO_NUM_18

#define PIN_USB_N GPIO_NUM_19
#define PIN_USB_P GPIO_NUM_20
