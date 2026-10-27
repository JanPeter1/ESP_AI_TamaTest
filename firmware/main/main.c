#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "cyd_bsp.h"
#include "cyd_config.h"
#include "cyd_wifi.h"
#include "cyd_presets.h"
#include "lvgl.h"

static const char *TAG = "MAIN";

#define LVGL_TICK_PERIOD_MS    2
#define LVGL_BUFFER_LINES      24
#define CYD_LCD_DIAGNOSTIC_PATTERN 0

static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_panel_io_handle_t io_handle = NULL;
static esp_lcd_touch_handle_t touch_handle = NULL;
static SemaphoreHandle_t lvgl_mux = NULL;
static SemaphoreHandle_t lcd_flush_sem = NULL;

static lv_obj_t *lbl_wifi_status = NULL;
static lv_obj_t *lbl_toast = NULL;
static lv_obj_t *modal_settings = NULL;
static lv_obj_t *modal_incoming = NULL;

static lv_color_t muted_accent(uint32_t color_hex) {
    return lv_color_darken(lv_color_hex(color_hex), 72);
}

static void set_dark_button_style(lv_obj_t *button, uint32_t bg_hex, uint32_t border_hex) {
    const uint32_t states[] = {
        LV_STATE_DEFAULT,
        LV_STATE_PRESSED,
        LV_STATE_FOCUSED,
        LV_STATE_CHECKED,
        LV_STATE_DISABLED,
    };
    for (size_t i = 0; i < sizeof(states) / sizeof(states[0]); i++) {
        lv_style_selector_t selector = LV_PART_MAIN | states[i];
        lv_obj_set_style_bg_color(button, lv_color_hex(bg_hex), selector);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, selector);
        lv_obj_set_style_border_color(button, lv_color_hex(border_hex), selector);
    }
}

#if CYD_LCD_DIAGNOSTIC_PATTERN
static void cyd_lcd_run_diagnostic_pattern(void) {
    const int lines = 24;
    const size_t pixels_per_block = CYD_LCD_H_RES * lines;
    uint8_t *buffer = heap_caps_malloc(pixels_per_block * 2, MALLOC_CAP_DMA);
    assert(buffer != NULL);

    for (int y = 0; y < CYD_LCD_V_RES; y += lines) {
        for (int row = 0; row < lines; row++) {
            for (int x = 0; x < CYD_LCD_H_RES; x++) {
                uint16_t color = x < CYD_LCD_H_RES / 3 ? 0xF800 :
                                 x < (CYD_LCD_H_RES * 2) / 3 ? 0x07E0 : 0x001F;
                size_t index = ((size_t)row * CYD_LCD_H_RES + x) * 2;
                buffer[index] = color >> 8;
                buffer[index + 1] = color & 0xFF;
            }
        }
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, 0, y, CYD_LCD_H_RES, y + lines, buffer));
        // tx_color is asynchronous; keep the DMA buffer unchanged until the
        // panel IO completion callback confirms the transfer has finished.
        xSemaphoreTake(lcd_flush_sem, portMAX_DELAY);
    }
    heap_caps_free(buffer);
}
#endif

// ==========================================
// Mutex Helpers for thread-safe LVGL access
// ==========================================
bool cyd_lvgl_lock(int timeout_ms) {
    if (!lvgl_mux) return false;
    const TickType_t timeout_ticks = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(lvgl_mux, timeout_ticks) == pdTRUE;
}

void cyd_lvgl_unlock(void) {
    if (lvgl_mux) {
        xSemaphoreGive(lvgl_mux);
    }
}

// ==========================================
// LVGL Tick Callback
// ==========================================
static void lvgl_tick_cb(void *arg) {
    (void)arg;
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

// ==========================================
// LVGL Flush & Touch Callbacks
// ==========================================
#if LVGL_VERSION_MAJOR >= 9
static bool lcd_color_trans_done_cb(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx) {
    (void)panel_io;
    (void)edata;
    (void)user_ctx;
    BaseType_t high_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(lcd_flush_sem, &high_task_woken);
    return high_task_woken == pdTRUE;
}

static void swap_rgb565_bytes(uint8_t *pixels, size_t pixel_count) {
    for (size_t i = 0; i < pixel_count; i++) {
        uint8_t high = pixels[i * 2];
        pixels[i * 2] = pixels[i * 2 + 1];
        pixels[i * 2 + 1] = high;
    }
}

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;
    size_t pixel_count = (size_t)(offsetx2 - offsetx1 + 1) * (size_t)(offsety2 - offsety1 + 1);
    swap_rgb565_bytes(px_map, pixel_count);
    esp_lcd_panel_draw_bitmap(panel, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, px_map);
    xSemaphoreTake(lcd_flush_sem, portMAX_DELAY);
    swap_rgb565_bytes(px_map, pixel_count);
    lv_display_flush_ready(disp);
}

static void lvgl_touch_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    esp_lcd_touch_handle_t tp = (esp_lcd_touch_handle_t)lv_indev_get_user_data(indev);
    uint16_t touch_x[1];
    uint16_t touch_y[1];
    uint16_t touch_strength[1];
    uint8_t touch_cnt = 0;

    esp_lcd_touch_read_data(tp);
    bool pressed = esp_lcd_touch_get_coordinates(tp, touch_x, touch_y, touch_strength, &touch_cnt, 1);
    if (pressed && touch_cnt > 0) {
        data->point.x = touch_x[0];
        data->point.y = touch_y[0];
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}
#else
static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map) {
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)drv->user_data;
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;
    esp_lcd_panel_draw_bitmap(panel, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);
    lv_disp_flush_ready(drv);
}

static void lvgl_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    esp_lcd_touch_handle_t tp = (esp_lcd_touch_handle_t)drv->user_data;
    uint16_t touch_x[1];
    uint16_t touch_y[1];
    uint16_t touch_strength[1];
    uint8_t touch_cnt = 0;

    esp_lcd_touch_read_data(tp);
    bool pressed = esp_lcd_touch_get_coordinates(tp, touch_x, touch_y, touch_strength, &touch_cnt, 1);
    if (pressed && touch_cnt > 0) {
        data->point.x = touch_x[0];
        data->point.y = touch_y[0];
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}
#endif

// ==========================================
// Toast / Feedback Label
// ==========================================
static void show_toast(const char *text) {
    if (lbl_toast) {
        lv_label_set_text(lbl_toast, text);
        lv_obj_clear_flag(lbl_toast, LV_OBJ_FLAG_HIDDEN);
    }
}

// ==========================================
// Incoming Message Popup Modal
// ==========================================
static void btn_close_incoming_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        if (modal_incoming) {
            lv_obj_delete(modal_incoming);
            modal_incoming = NULL;
        }
        cyd_bsp_rgb_off();
    }
}

void cyd_ui_show_incoming_msg(const char *sender, const char *icon, const char *msg, uint32_t color_hex, uint8_t led_code) {
    if (!cyd_lvgl_lock(100)) return;

    if (modal_incoming) {
        lv_obj_delete(modal_incoming);
        modal_incoming = NULL;
    }

    // LED codes use the same RGB bit mask as the preset buttons.
    cyd_bsp_rgb_set((led_code & 0x01) != 0,
                    (led_code & 0x02) != 0,
                    (led_code & 0x04) != 0);

    // Play pleasant 3-tone notification chime
    cyd_bsp_play_chime();

    modal_incoming = lv_obj_create(lv_screen_active());
    lv_obj_set_size(modal_incoming, 290, 190);
    lv_obj_center(modal_incoming);
    lv_obj_set_style_bg_color(modal_incoming, lv_color_hex(0x111722), 0);
    lv_obj_set_style_border_color(modal_incoming, muted_accent(color_hex), 0);
    lv_obj_set_style_border_width(modal_incoming, 3, 0);

    // Header: Sender
    lv_obj_t *title = lv_label_create(modal_incoming);
    char title_buf[64];
    snprintf(title_buf, sizeof(title_buf), "Nachricht von %s", sender ? sender : "Partner");
    lv_label_set_text(title, title_buf);
    lv_obj_set_style_text_color(title, lv_color_hex(0xA8873C), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    // Icon & Message Text
    lv_obj_t *lbl_icon = lv_label_create(modal_incoming);
    lv_label_set_text(lbl_icon, icon ? icon : ":-)");
    lv_obj_set_style_text_color(lbl_icon, muted_accent(color_hex), 0);
    lv_obj_align(lbl_icon, LV_ALIGN_CENTER, 0, -25);

    lv_obj_t *lbl_msg = lv_label_create(modal_incoming);
    lv_label_set_text(lbl_msg, msg ? msg : "");
    lv_obj_set_style_text_color(lbl_msg, lv_color_hex(0xC4CAD4), 0);
    lv_obj_align(lbl_msg, LV_ALIGN_CENTER, 0, 10);

    // Close button
    lv_obj_t *btn_close = lv_button_create(modal_incoming);
    lv_obj_set_size(btn_close, 110, 34);
    lv_obj_align(btn_close, LV_ALIGN_BOTTOM_MID, 0, 0);
    set_dark_button_style(btn_close, 0x151B26, 0x334155);
    lv_obj_add_event_cb(btn_close, btn_close_incoming_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_btn = lv_label_create(btn_close);
    lv_label_set_text(lbl_btn, "OK / Schliessen");
    lv_obj_center(lbl_btn);

    cyd_lvgl_unlock();
}

// ==========================================
// Settings Modal Window
// ==========================================
static void btn_close_settings_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        if (modal_settings) {
            lv_obj_delete(modal_settings);
            modal_settings = NULL;
        }
    }
}

static void show_ap_portal_modal(const char *ap_ssid) {
    if (modal_settings) return;

    modal_settings = lv_obj_create(lv_screen_active());
    lv_obj_set_size(modal_settings, 290, 210);
    lv_obj_center(modal_settings);
    lv_obj_set_style_bg_color(modal_settings, lv_color_hex(0x111722), 0);
    lv_obj_set_style_border_color(modal_settings, lv_color_hex(0x6B5528), 0);
    lv_obj_set_style_border_width(modal_settings, 2, 0);

    lv_obj_t *title = lv_label_create(modal_settings);
    lv_label_set_text(title, "WLAN Setup-Hotspot aktiv");
    lv_obj_set_style_text_color(title, lv_color_hex(0xA8873C), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);

    lv_obj_t *info = lv_label_create(modal_settings);
    char buf[128];
    snprintf(buf, sizeof(buf), "1. Mit Hotspot verbinden:\n   SSID: %s\n2. Browser oeffnet sich auto.\n   (oder 192.168.4.1 aufrufen)", ap_ssid);
    lv_label_set_text(info, buf);
    lv_obj_set_style_text_color(info, lv_color_hex(0xC4CAD4), 0);
    lv_obj_align(info, LV_ALIGN_TOP_LEFT, 5, 35);

    lv_obj_t *btn_close = lv_button_create(modal_settings);
    lv_obj_set_size(btn_close, 120, 36);
    lv_obj_align(btn_close, LV_ALIGN_BOTTOM_MID, 0, -5);
    set_dark_button_style(btn_close, 0x151B26, 0x334155);
    lv_obj_add_event_cb(btn_close, btn_close_settings_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_close = lv_label_create(btn_close);
    lv_label_set_text(lbl_close, "Schliessen");
    lv_obj_center(lbl_close);
}

static void btn_settings_event_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Settings button tapped: starting SoftAP portal...");
        cyd_bsp_rgb_set(false, false, true);
        cyd_bsp_play_tone(880, 80);
        cyd_wifi_start_ap_portal();
    }
}

// ==========================================
// Preset Button Click Handler
// ==========================================
static void btn_preset_event_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        int idx = (int)(intptr_t)lv_event_get_user_data(e);
        if (idx >= 0 && idx < CYD_PRESETS_COUNT) {
            const cyd_preset_t *preset = &CYD_PRESETS[idx];
            ESP_LOGI(TAG, "Sending Preset [%s]: %s", preset->id, preset->message);

            // Audio & LED Feedback
            cyd_bsp_play_tone(1175, 70); // High D6 chime click
            cyd_bsp_rgb_set((preset->led_code & 0x01) != 0,
                            (preset->led_code & 0x02) != 0,
                            (preset->led_code & 0x04) != 0);

            char toast_buf[64];
            snprintf(toast_buf, sizeof(toast_buf), "Gesendet: %s", preset->title);
            show_toast(toast_buf);

            // In background task: turn LED off after 400ms
            vTaskDelay(pdMS_TO_TICKS(400));
            cyd_bsp_rgb_off();
        }
    }
}

// ==========================================
// Wi-Fi State Callback
// ==========================================
static void on_wifi_state_changed(cyd_wifi_state_t state, const char *info_str) {
    if (!cyd_lvgl_lock(100)) return;

    if (lbl_wifi_status) {
        char buf[64];
        switch (state) {
            case CYD_WIFI_STATE_CONNECTING:
                snprintf(buf, sizeof(buf), "Verbinde: %s...", info_str);
                lv_label_set_text(lbl_wifi_status, buf);
                lv_obj_set_style_text_color(lbl_wifi_status, lv_color_hex(0xA8873C), 0);
                break;
            case CYD_WIFI_STATE_CONNECTED:
                snprintf(buf, sizeof(buf), "Online (%s)", info_str);
                lv_label_set_text(lbl_wifi_status, buf);
                lv_obj_set_style_text_color(lbl_wifi_status, lv_color_hex(0x347A57), 0);
                cyd_bsp_rgb_set(false, true, false);
                vTaskDelay(pdMS_TO_TICKS(200));
                cyd_bsp_rgb_off();
                if (modal_settings) {
                    lv_obj_delete(modal_settings);
                    modal_settings = NULL;
                }
                break;
            case CYD_WIFI_STATE_AP_PORTAL:
                snprintf(buf, sizeof(buf), "AP: %s", info_str);
                lv_label_set_text(lbl_wifi_status, buf);
                lv_obj_set_style_text_color(lbl_wifi_status, lv_color_hex(0x965316), 0);
                show_ap_portal_modal(info_str);
                break;
            default:
                lv_label_set_text(lbl_wifi_status, "Offline");
                lv_obj_set_style_text_color(lbl_wifi_status, lv_color_hex(0x9A4249), 0);
                break;
        }
    }

    cyd_lvgl_unlock();
}

// ==========================================
// Build UI (DeskConnect Touch Screen)
// ==========================================
static void create_demo_ui(void) {
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x080A0F), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    // --- Header Bar (Height ~30px) ---
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "DeskConnect");
    lv_obj_set_style_text_color(title, lv_color_hex(0xA8873C), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 8, 7);

    lbl_wifi_status = lv_label_create(scr);
    lv_label_set_text(lbl_wifi_status, "WLAN...");
    lv_obj_set_style_text_color(lbl_wifi_status, lv_color_hex(0x596579), 0);
    lv_obj_align(lbl_wifi_status, LV_ALIGN_TOP_LEFT, 110, 8);

    lv_obj_t *btn_settings = lv_button_create(scr);
    lv_obj_set_size(btn_settings, 70, 24);
    lv_obj_align(btn_settings, LV_ALIGN_TOP_RIGHT, -6, 4);
    lv_obj_add_event_cb(btn_settings, btn_settings_event_cb, LV_EVENT_CLICKED, NULL);
    set_dark_button_style(btn_settings, 0x10151E, 0x334155);
    lv_obj_t *lbl_settings = lv_label_create(btn_settings);
    lv_label_set_text(lbl_settings, "Setup");
    lv_obj_set_style_text_color(lbl_settings, lv_color_hex(0xC4CAD4), 0);
    lv_obj_center(lbl_settings);

    // --- 8 Preset Status Cards in 4x2 Grid ---
    // Screen: 320 x 240. Usable vertical space: y=34 to 220.
    const int btn_w = 72;
    const int btn_h = 90;
    const int pad_x = 6;
    const int pad_y = 6;
    const int start_x = 6;
    const int start_y = 34;

    for (int i = 0; i < CYD_PRESETS_COUNT; i++) {
        int col = i % 4;
        int row = i / 4;
        int x = start_x + col * (btn_w + pad_x);
        int y = start_y + row * (btn_h + pad_y);

        const cyd_preset_t *preset = &CYD_PRESETS[i];

        lv_obj_t *btn = lv_button_create(scr);
        lv_obj_set_size(btn, btn_w, btn_h);
        lv_obj_set_pos(btn, x, y);
        set_dark_button_style(btn, 0x0B0F15, lv_color_to_u32(muted_accent(preset->color_hex)));
        lv_obj_set_style_border_width(btn, 2, 0);
        lv_obj_set_style_radius(btn, 8, 0);

        lv_obj_add_event_cb(btn, btn_preset_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        // Icon Label
        lv_obj_t *lbl_icon = lv_label_create(btn);
        lv_label_set_text(lbl_icon, preset->icon);
        lv_obj_set_style_text_color(lbl_icon, muted_accent(preset->color_hex), 0);
        lv_obj_align(lbl_icon, LV_ALIGN_TOP_MID, 0, 4);

        // Short Title Label
        lv_obj_t *lbl_title = lv_label_create(btn);
        lv_label_set_text(lbl_title, preset->title);
        lv_label_set_long_mode(lbl_title, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(lbl_title, btn_w - 8);
        lv_obj_set_style_text_align(lbl_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl_title, lv_color_hex(0xC4CAD4), 0);
        lv_obj_align(lbl_title, LV_ALIGN_BOTTOM_MID, 0, -3);
    }

    // Toast feedback label (bottom bar)
    lbl_toast = lv_label_create(scr);
    lv_label_set_text(lbl_toast, "Tippe auf eine Karte zum Senden");
    lv_obj_set_style_text_color(lbl_toast, lv_color_hex(0x596579), 0);
    lv_obj_align(lbl_toast, LV_ALIGN_BOTTOM_MID, 0, -2);
}

// ==========================================
// LVGL Main Task
// ==========================================
static void lvgl_task(void *pvParameters) {
    ESP_LOGI(TAG, "Starting LVGL task loop...");
    while (1) {
        if (cyd_lvgl_lock(-1)) {
            lv_timer_handler();
            cyd_lvgl_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ==========================================
// app_main
// ==========================================
void app_main(void) {
    ESP_LOGI(TAG, "Booting DeskConnect CYD on ESP32-2432S028...");

    // 1. Init NVS Config Storage
    ESP_ERROR_CHECK(cyd_config_init());

    // 2. Init BSP
    cyd_bsp_init();
    cyd_bsp_rgb_set(false, true, false);
    cyd_bsp_play_chime();
    cyd_bsp_rgb_off();

    // 3. Init Display & Touch
    ESP_ERROR_CHECK(cyd_bsp_display_init(&panel_handle, &io_handle));
#if CYD_LCD_DIAGNOSTIC_PATTERN
    lcd_flush_sem = xSemaphoreCreateBinary();
    assert(lcd_flush_sem != NULL);
    const esp_lcd_panel_io_callbacks_t diagnostic_callbacks = {
        .on_color_trans_done = lcd_color_trans_done_cb,
    };
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(io_handle, &diagnostic_callbacks, NULL));
    cyd_lcd_run_diagnostic_pattern();
    while (true) vTaskDelay(pdMS_TO_TICKS(1000));
#endif
    ESP_ERROR_CHECK(cyd_bsp_touch_init(&touch_handle));

    // 4. Init LVGL
    lvgl_mux = xSemaphoreCreateMutex();
    lcd_flush_sem = xSemaphoreCreateBinary();
    assert(lvgl_mux != NULL && lcd_flush_sem != NULL);
    lv_init();

    size_t buf_size = CYD_LCD_H_RES * LVGL_BUFFER_LINES * sizeof(lv_color_t);
    lv_color_t *buf1 = heap_caps_malloc(buf_size, MALLOC_CAP_DMA);
    lv_color_t *buf2 = heap_caps_malloc(buf_size, MALLOC_CAP_DMA);
    assert(buf1 != NULL);

#if LVGL_VERSION_MAJOR >= 9
    lv_display_t *disp = lv_display_create(CYD_LCD_H_RES, CYD_LCD_V_RES);
    lv_display_set_theme(disp, lv_theme_default_init(disp,
        lv_color_hex(0x6B5528),
        lv_color_hex(0x263244), true, LV_FONT_DEFAULT));
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, buf1, buf2, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, lvgl_flush_cb);
    lv_display_set_user_data(disp, panel_handle);

    const esp_lcd_panel_io_callbacks_t lcd_io_callbacks = {
        .on_color_trans_done = lcd_color_trans_done_cb,
    };
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(io_handle, &lcd_io_callbacks, NULL));

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, lvgl_touch_cb);
    lv_indev_set_user_data(indev, touch_handle);
#else
    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, CYD_LCD_H_RES * LVGL_BUFFER_LINES);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = CYD_LCD_H_RES;
    disp_drv.ver_res = CYD_LCD_V_RES;
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.user_data = panel_handle;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = lvgl_touch_cb;
    indev_drv.user_data = touch_handle;
    lv_indev_drv_register(&indev_drv);
#endif

    // 5. Tick Timer
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &lvgl_tick_cb,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000));

    // 6. Build UI
    if (cyd_lvgl_lock(-1)) {
        create_demo_ui();
        cyd_lvgl_unlock();
    }

    // 7. Start LVGL Task
    xTaskCreatePinnedToCore(lvgl_task, "lvgl_task", 8192, NULL, 5, NULL, 1);

    // 8. Init Wi-Fi & Auto-Connect / AP Fallback
    ESP_ERROR_CHECK(cyd_wifi_init(on_wifi_state_changed));
    cyd_wifi_connect_sta();

    ESP_LOGI(TAG, "CYD initialization completed.");
}
