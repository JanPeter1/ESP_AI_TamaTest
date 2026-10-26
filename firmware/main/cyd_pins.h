#pragma once

#include "driver/gpio.h"

// ==========================================
// ESP32-2432S028 Pinout Definition
// ==========================================

// Display SPI (HSPI / SPI2)
#define CYD_LCD_HOST            SPI2_HOST
#define CYD_LCD_MOSI            GPIO_NUM_13
#define CYD_LCD_MISO            GPIO_NUM_12
#define CYD_LCD_SCLK            GPIO_NUM_14
#define CYD_LCD_CS              GPIO_NUM_15
#define CYD_LCD_DC              GPIO_NUM_2
#define CYD_LCD_RST             (-1)
#define CYD_LCD_BCKL            GPIO_NUM_21

// Display Resolution
#define CYD_LCD_H_RES           320
#define CYD_LCD_V_RES           240
#define CYD_LCD_PIXEL_CLOCK_HZ  (40 * 1000 * 1000) // 40 MHz SPI

// Touchscreen SPI (XPT2046 on SPI3 / VSPI via GPIO Matrix)
#define CYD_TOUCH_HOST          SPI3_HOST
#define CYD_TOUCH_SCLK          GPIO_NUM_25
#define CYD_TOUCH_CS            GPIO_NUM_33
#define CYD_TOUCH_MOSI          GPIO_NUM_32
#define CYD_TOUCH_MISO          GPIO_NUM_39
#define CYD_TOUCH_IRQ           GPIO_NUM_36

// On-board Peripherals
#define CYD_LED_RED             GPIO_NUM_4   // Active LOW
#define CYD_LED_GREEN           GPIO_NUM_16  // Active LOW
#define CYD_LED_BLUE            GPIO_NUM_17  // Active LOW

#define CYD_LDR_PIN             GPIO_NUM_34  // ADC1 Channel 6
#define CYD_AUDIO_PIN           GPIO_NUM_26  // DAC2 / PWM Audio

// MicroSD Slot (VSPI native pins)
#define CYD_SD_CS               GPIO_NUM_5
#define CYD_SD_MOSI             GPIO_NUM_23
#define CYD_SD_MISO             GPIO_NUM_19
#define CYD_SD_SCK              GPIO_NUM_18
