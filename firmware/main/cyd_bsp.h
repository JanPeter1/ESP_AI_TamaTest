#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "cyd_pins.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"

#ifdef __cplusplus
extern "C" {
#endif

// ==========================================
// Board Support Package (BSP) Functions
// ==========================================

/**
 * @brief Initialize all basic on-board peripherals (LEDs, Backlight, Audio, LDR)
 */
esp_err_t cyd_bsp_init(void);

// --- Backlight Control ---
esp_err_t cyd_bsp_backlight_init(void);
esp_err_t cyd_bsp_backlight_set(uint8_t percent); // 0 to 100%

// --- RGB LED Control (Active LOW) ---
esp_err_t cyd_bsp_rgb_init(void);
void cyd_bsp_rgb_set(bool red, bool green, bool blue);
void cyd_bsp_rgb_off(void);

// --- Audio / Chime (GPIO 26) ---
esp_err_t cyd_bsp_audio_init(void);
void cyd_bsp_play_tone(uint32_t freq_hz, uint32_t duration_ms);
void cyd_bsp_play_chime(void);

// --- Light Sensor (LDR GPIO 34) ---
int cyd_bsp_ldr_read_raw(void);

// --- Display & Touch Handles ---
esp_err_t cyd_bsp_display_init(esp_lcd_panel_handle_t *ret_panel, esp_lcd_panel_io_handle_t *ret_io);
esp_err_t cyd_bsp_touch_init(esp_lcd_touch_handle_t *ret_touch);

#ifdef __cplusplus
}
#endif
