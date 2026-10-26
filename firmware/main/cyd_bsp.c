#include "cyd_bsp.h"
#include "esp_log.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_touch_xpt2046.h"
#include "esp_adc/adc_oneshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "CYD_BSP";

// LEDC configuration for Backlight
#define LCD_BCKL_LEDC_TIMER       LEDC_TIMER_0
#define LCD_BCKL_LEDC_MODE        LEDC_LOW_SPEED_MODE
#define LCD_BCKL_LEDC_CHANNEL     LEDC_CHANNEL_0
#define LCD_BCKL_LEDC_DUTY_RES    LEDC_TIMER_10_BIT // 0 - 1023
#define LCD_BCKL_LEDC_FREQ        5000              // 5 kHz

// LEDC configuration for Audio/Buzzer
#define AUDIO_LEDC_TIMER          LEDC_TIMER_1
#define AUDIO_LEDC_MODE           LEDC_LOW_SPEED_MODE
#define AUDIO_LEDC_CHANNEL        LEDC_CHANNEL_1
#define AUDIO_LEDC_DUTY_RES       LEDC_TIMER_10_BIT

static adc_oneshot_unit_handle_t adc1_handle = NULL;

esp_err_t cyd_bsp_init(void) {
    ESP_LOGI(TAG, "Initializing CYD BSP hardware (ESP32-2432S028)...");
    ESP_ERROR_CHECK(cyd_bsp_rgb_init());
    ESP_ERROR_CHECK(cyd_bsp_backlight_init());
    ESP_ERROR_CHECK(cyd_bsp_audio_init());

    // ADC for LDR
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    adc_oneshot_new_unit(&init_config1, &adc1_handle);

    adc_oneshot_chan_cfg_t chan_config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_6, &chan_config); // GPIO 34

    return ESP_OK;
}

// ==========================================
// Backlight Control
// ==========================================
esp_err_t cyd_bsp_backlight_init(void) {
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LCD_BCKL_LEDC_MODE,
        .timer_num        = LCD_BCKL_LEDC_TIMER,
        .duty_resolution  = LCD_BCKL_LEDC_DUTY_RES,
        .freq_hz          = LCD_BCKL_LEDC_FREQ,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LCD_BCKL_LEDC_MODE,
        .channel        = LCD_BCKL_LEDC_CHANNEL,
        .timer_sel      = LCD_BCKL_LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = CYD_LCD_BCKL,
        .duty           = 0, // start off
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    // Default to 80% brightness
    return cyd_bsp_backlight_set(80);
}

esp_err_t cyd_bsp_backlight_set(uint8_t percent) {
    if (percent > 100) percent = 100;
    uint32_t duty = (percent * 1023) / 100;
    ESP_ERROR_CHECK(ledc_set_duty(LCD_BCKL_LEDC_MODE, LCD_BCKL_LEDC_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LCD_BCKL_LEDC_MODE, LCD_BCKL_LEDC_CHANNEL));
    return ESP_OK;
}

// ==========================================
// RGB LED Control (Active LOW)
// ==========================================
esp_err_t cyd_bsp_rgb_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << CYD_LED_RED) | (1ULL << CYD_LED_GREEN) | (1ULL << CYD_LED_BLUE),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    cyd_bsp_rgb_off();
    return ESP_OK;
}

void cyd_bsp_rgb_set(bool red, bool green, bool blue) {
    // Active LOW: 0 = LED on, 1 = LED off
    gpio_set_level(CYD_LED_RED,   red   ? 0 : 1);
    gpio_set_level(CYD_LED_GREEN, green ? 0 : 1);
    gpio_set_level(CYD_LED_BLUE,  blue  ? 0 : 1);
}

void cyd_bsp_rgb_off(void) {
    cyd_bsp_rgb_set(false, false, false);
}

// ==========================================
// Audio / Chime Control (GPIO 26)
// ==========================================
esp_err_t cyd_bsp_audio_init(void) {
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = AUDIO_LEDC_MODE,
        .timer_num        = AUDIO_LEDC_TIMER,
        .duty_resolution  = AUDIO_LEDC_DUTY_RES,
        .freq_hz          = 1000,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {
        .speed_mode     = AUDIO_LEDC_MODE,
        .channel        = AUDIO_LEDC_CHANNEL,
        .timer_sel      = AUDIO_LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = CYD_AUDIO_PIN,
        .duty           = 0,
        .hpoint         = 0
    };
    return ledc_channel_config(&ledc_channel);
}

void cyd_bsp_play_tone(uint32_t freq_hz, uint32_t duration_ms) {
    if (freq_hz > 0) {
        ledc_set_freq(AUDIO_LEDC_MODE, AUDIO_LEDC_TIMER, freq_hz);
        ledc_set_duty(AUDIO_LEDC_MODE, AUDIO_LEDC_CHANNEL, 512); // 50% duty
        ledc_update_duty(AUDIO_LEDC_MODE, AUDIO_LEDC_CHANNEL);
        vTaskDelay(pdMS_TO_TICKS(duration_ms));
    }
    // Turn off
    ledc_set_duty(AUDIO_LEDC_MODE, AUDIO_LEDC_CHANNEL, 0);
    ledc_update_duty(AUDIO_LEDC_MODE, AUDIO_LEDC_CHANNEL);
}

void cyd_bsp_play_chime(void) {
    // 3-tone notification chime (D5, A5, D6)
    cyd_bsp_play_tone(587, 80);
    vTaskDelay(pdMS_TO_TICKS(30));
    cyd_bsp_play_tone(880, 80);
    vTaskDelay(pdMS_TO_TICKS(30));
    cyd_bsp_play_tone(1175, 140);
}

// ==========================================
// Light Sensor (LDR)
// ==========================================
int cyd_bsp_ldr_read_raw(void) {
    int val = 0;
    if (adc1_handle) {
        adc_oneshot_read(adc1_handle, ADC_CHANNEL_6, &val);
    }
    return val;
}

// ==========================================
// Display (ILI9341 via SPI2)
// ==========================================
esp_err_t cyd_bsp_display_init(esp_lcd_panel_handle_t *ret_panel, esp_lcd_panel_io_handle_t *ret_io) {
    ESP_LOGI(TAG, "Initializing SPI bus for LCD...");
    spi_bus_config_t buscfg = {
        .sclk_io_num = CYD_LCD_SCLK,
        .mosi_io_num = CYD_LCD_MOSI,
        .miso_io_num = CYD_LCD_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = CYD_LCD_H_RES * 40 * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(CYD_LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "Attaching ILI9341 to SPI bus...");
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = CYD_LCD_DC,
        .cs_gpio_num = CYD_LCD_CS,
        .pclk_hz = CYD_LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)CYD_LCD_HOST, &io_config, ret_io));

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = CYD_LCD_RST,
        .rgb_endian = LCD_RGB_ENDIAN_BGR,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(*ret_io, &panel_config, ret_panel));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(*ret_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(*ret_panel));
    // Landscape orientation: 320 x 240
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(*ret_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(*ret_panel, false, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(*ret_panel, true));

    ESP_LOGI(TAG, "LCD initialized successfully.");
    return ESP_OK;
}

// ==========================================
// Touch (XPT2046 via SPI3)
// ==========================================
esp_err_t cyd_bsp_touch_init(esp_lcd_touch_handle_t *ret_touch) {
    ESP_LOGI(TAG, "Initializing SPI bus for Touchscreen...");
    spi_bus_config_t buscfg = {
        .sclk_io_num = CYD_TOUCH_SCLK,
        .mosi_io_num = CYD_TOUCH_MOSI,
        .miso_io_num = CYD_TOUCH_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 128,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(CYD_TOUCH_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t tp_io_config = {
        .cs_gpio_num = CYD_TOUCH_CS,
        .pclk_hz = 2 * 1000 * 1000, // 2 MHz
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 5,
    };
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)CYD_TOUCH_HOST, &tp_io_config, &tp_io_handle));

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = CYD_LCD_H_RES,
        .y_max = CYD_LCD_V_RES,
        .rst_gpio_num = -1,
        .int_gpio_num = CYD_TOUCH_IRQ,
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };
    ESP_ERROR_CHECK(esp_lcd_touch_new_spi_xpt2046(tp_io_handle, &tp_cfg, ret_touch));

    ESP_LOGI(TAG, "Touch controller initialized successfully.");
    return ESP_OK;
}
