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
#include "cyd_messages.h"
#include "cyd_avatars.h"
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
static lv_obj_t *lbl_send_wifi_status = NULL;
static lv_obj_t *lbl_server_status = NULL;
static lv_obj_t *lbl_toast = NULL;
static lv_obj_t *modal_settings = NULL;
static lv_obj_t *modal_incoming = NULL;
static lv_obj_t *home_screen = NULL;
static lv_obj_t *send_screen = NULL;
static lv_obj_t *send_grid = NULL;
static lv_obj_t *home_avatar = NULL;
static lv_obj_t *home_sender = NULL;
static lv_obj_t *home_message = NULL;
static lv_obj_t *home_emotion = NULL;
static lv_obj_t *home_previous = NULL;
static lv_obj_t *setup_buttons[2] = {0};
static lv_obj_t *setup_labels[2] = {0};
static bool setup_ap_active = false;
static lv_obj_t *category_buttons[4] = {0};
static bool enabled_categories[4] = {true, true, true, true};
static cyd_room_message_t room_feed[8];
static size_t room_feed_count = 0;
static char local_device_id[32] = "CYD_Display";

static void add_room_message_locked(const cyd_room_message_t *message);
static void refresh_home_messages(void);
static void switch_to_send_screen(lv_event_t *event);
static void switch_to_home_screen(lv_event_t *event);
static void set_dark_button_style(lv_obj_t *button, uint32_t bg_hex, uint32_t border_hex);

static void style_setup_buttons(void) {
    const uint32_t bg = setup_ap_active ? 0x7F1D1D : 0x10151E;
    const uint32_t border = setup_ap_active ? 0xEF4444 : 0x334155;
    const uint32_t text = setup_ap_active ? 0xFFFFFF : 0xC4CAD4;
    for (size_t i = 0; i < 2; i++) {
        if (setup_buttons[i]) set_dark_button_style(setup_buttons[i], bg, border);
        if (setup_labels[i]) lv_obj_set_style_text_color(setup_labels[i], lv_color_hex(text), 0);
    }
}

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

static void start_ap_portal_task(void *arg) {
    (void)arg;
    esp_err_t err = cyd_wifi_start_ap_portal();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not start setup AP: %s", esp_err_to_name(err));
    }
    vTaskDelete(NULL);
}

static void btn_settings_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *button = lv_event_get_target(e);
    if (code == LV_EVENT_PRESSED) {
        set_dark_button_style(button, 0x7F1D1D, 0xEF4444);
        lv_obj_t *label = lv_obj_get_child(button, 0);
        if (label) lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    } else if (code == LV_EVENT_RELEASED) {
        style_setup_buttons();
    } else if (code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Settings button clicked; queueing SoftAP start...");
        cyd_bsp_rgb_set(false, false, true);
        cyd_bsp_play_tone(880, 80);
        if (xTaskCreate(start_ap_portal_task, "setup_ap", 4096, NULL, 5, NULL) != pdPASS) {
            ESP_LOGE(TAG, "Could not create setup AP task");
        }
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

            bool sent = cyd_messages_send_preset(preset);

            // Audio & LED Feedback
            cyd_bsp_play_tone(1175, 70); // High D6 chime click
            cyd_bsp_rgb_set((preset->led_code & 0x01) != 0,
                            (preset->led_code & 0x02) != 0,
                            (preset->led_code & 0x04) != 0);

            char toast_buf[64];
            snprintf(toast_buf, sizeof(toast_buf), "%s: %s", sent ? "Gesendet" : "Nur lokal", preset->title);
            show_toast(toast_buf);

            if (!sent) {
                cyd_room_message_t local_message = {0};
                snprintf(local_message.sender_id, sizeof(local_message.sender_id), "%s", local_device_id);
                snprintf(local_message.sender_name, sizeof(local_message.sender_name), "%s", local_device_id);
                snprintf(local_message.text, sizeof(local_message.text), "%s", preset->message);
                local_message.avatar_id = (uint8_t)(local_device_id[0] & 1);
                add_room_message_locked(&local_message);
            }

            lv_screen_load_anim(home_screen, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, 180, 0, false);

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
    setup_ap_active = state == CYD_WIFI_STATE_AP_PORTAL;

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
        if (lbl_send_wifi_status) lv_label_set_text(lbl_send_wifi_status, buf);
    }
    style_setup_buttons();

    cyd_lvgl_unlock();
    if (state == CYD_WIFI_STATE_CONNECTED) {
        esp_err_t err = cyd_messages_start();
        if (err != ESP_OK) ESP_LOGE(TAG, "Could not start room messaging: %s", esp_err_to_name(err));
    }
}

// ==========================================
// Two-page room UI
// ==========================================
static void set_screen_background(lv_obj_t *screen) {
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x080A0F), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
}

static void add_header(lv_obj_t *screen, bool home) {
    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, home ? "DeskConnect" : "Nachricht senden");
    lv_obj_set_style_text_color(title, lv_color_hex(0xD7AD54), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 7, 6);

    lv_obj_t *status = lv_label_create(screen);
    lv_label_set_text(status, "WLAN...");
    lv_obj_set_style_text_color(status, lv_color_hex(0x596579), 0);
    lv_obj_set_width(status, 104);
    lv_label_set_long_mode(status, LV_LABEL_LONG_DOT);
    lv_obj_align(status, LV_ALIGN_TOP_LEFT, home ? 108 : 143, 7);
    if (home) lbl_wifi_status = status;
    else lbl_send_wifi_status = status;

    lv_obj_t *setup = lv_button_create(screen);
    lv_obj_set_size(setup, 70, 30);
    lv_obj_align(setup, LV_ALIGN_TOP_RIGHT, -5, 3);
    lv_obj_add_event_cb(setup, btn_settings_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(setup, btn_settings_event_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(setup, btn_settings_event_cb, LV_EVENT_CLICKED, NULL);
    const size_t setup_idx = home ? 0 : 1;
    setup_buttons[setup_idx] = setup;
    lv_obj_t *setup_label = lv_label_create(setup);
    lv_label_set_text(setup_label, "Setup");
    setup_labels[setup_idx] = setup_label;
    style_setup_buttons();
    lv_obj_center(setup_label);
}

static void home_gesture_cb(lv_event_t *event) {
    if (lv_event_get_code(event) == LV_EVENT_GESTURE &&
        lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_LEFT) {
        lv_indev_wait_release(lv_indev_active());
        lv_screen_load_anim(send_screen, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 180, 0, false);
    }
}

static void send_gesture_cb(lv_event_t *event) {
    if (lv_event_get_code(event) == LV_EVENT_GESTURE &&
        lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_RIGHT) {
        lv_indev_wait_release(lv_indev_active());
        lv_screen_load_anim(home_screen, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, 180, 0, false);
    }
}

static int preset_category_index(const char *preset_id) {
    if (strcmp(preset_id, "alles_gut") == 0 || strcmp(preset_id, "schlafen") == 0) return 0;
    if (strcmp(preset_id, "hab_dich_lieb") == 0 || strcmp(preset_id, "feierabend") == 0) return 2;
    if (strcmp(preset_id, "beschaeftigt") == 0 || strcmp(preset_id, "gleich_da") == 0) return 3;
    return 1;
}

static void rebuild_preset_grid(void) {
    if (!send_grid) return;
    lv_obj_clean(send_grid);

    int visible_index = 0;
    const int button_w = 74;
    const int button_h = 70;
    for (int i = 0; i < CYD_PRESETS_COUNT; ++i) {
        const cyd_preset_t *preset = &CYD_PRESETS[i];
        if (!enabled_categories[preset_category_index(preset->id)]) continue;

        lv_obj_t *button = lv_button_create(send_grid);
        lv_obj_set_size(button, button_w, button_h);
        lv_obj_set_pos(button, (visible_index % 4) * 78, (visible_index / 4) * 74);
        set_dark_button_style(button, 0x0B0F15, lv_color_to_u32(muted_accent(preset->color_hex)));
        lv_obj_set_style_border_width(button, 2, 0);
        lv_obj_set_style_radius(button, 8, 0);
        lv_obj_add_event_cb(button, btn_preset_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *icon = lv_label_create(button);
        lv_label_set_text(icon, preset->icon);
        lv_obj_set_style_text_color(icon, muted_accent(preset->color_hex), 0);
        lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 3);

        lv_obj_t *label = lv_label_create(button);
        lv_label_set_text(label, preset->title);
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(label, button_w - 6);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_color(label, lv_color_hex(0xD5DAE3), 0);
        lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -2);
        visible_index++;
    }
}

static void category_button_cb(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    int category = (int)(intptr_t)lv_event_get_user_data(event);
    if (category < 0 || category >= 4) return;

    bool next = !enabled_categories[category];
    int enabled_count = 0;
    for (int i = 0; i < 4; ++i) enabled_count += enabled_categories[i] ? 1 : 0;
    if (!next && enabled_count == 1) {
        show_toast("Mindestens ein Set muss aktiv bleiben");
        return;
    }
    enabled_categories[category] = next;
    set_dark_button_style(category_buttons[category], next ? 0x1C2B38 : 0x10151E,
                          next ? 0x607D8B : 0x334155);
    rebuild_preset_grid();
}

static void switch_to_send_screen(lv_event_t *event) {
    (void)event;
    lv_screen_load_anim(send_screen, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 180, 0, false);
}

static void switch_to_home_screen(lv_event_t *event) {
    (void)event;
    lv_screen_load_anim(home_screen, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, 180, 0, false);
}

static void refresh_home_messages(void) {
    if (room_feed_count == 0) {
        lv_label_set_text(home_sender, "Willkommen im Raum");
        lv_label_set_text(home_message, "Noch keine Nachrichten.\nWische nach links zum Senden.");
        lv_label_set_text(home_previous, "Neue Nachrichten aus dem Raum erscheinen hier.");
        lv_label_set_text(home_emotion, "...");
        lv_image_set_src(home_avatar, &cyd_avatar_cat);
        return;
    }

    char message_text[160];
    snprintf(message_text, sizeof(message_text), "%.*s",
             (int)sizeof(message_text) - 1, room_feed[0].text);
    lv_label_set_text(home_sender, room_feed[0].sender_name);
    lv_label_set_text(home_message, message_text);
    lv_image_set_src(home_avatar, cyd_avatar_get(room_feed[0].avatar_id));

    const char *expression = "...";
    if (strcmp(room_feed[0].emotion, "happy") == 0) expression = ":D";
    if (strcmp(room_feed[0].emotion, "sleepy") == 0) expression = "Zzz";
    else if (strcmp(room_feed[0].emotion, "hungry") == 0) expression = "...";
    else if (strcmp(room_feed[0].emotion, "curious") == 0) expression = "?";
    else if (strcmp(room_feed[0].emotion, "love") == 0) expression = "<3";
    else if (strcmp(room_feed[0].emotion, "busy") == 0) expression = "!";
    else if (strcmp(room_feed[0].emotion, "wave") == 0) expression = "-->";
    lv_label_set_text(home_emotion, expression);

    if (room_feed_count > 1) {
        char previous_text[160];
        snprintf(previous_text, sizeof(previous_text), "Zuletzt: %.31s: %.110s",
                 room_feed[1].sender_name, room_feed[1].text);
        lv_label_set_text(home_previous, previous_text);
    } else {
        lv_label_set_text(home_previous, "Dein Raum ist verbunden — tippe zum Antworten.");
    }
}

static void add_room_message_locked(const cyd_room_message_t *message) {
    if (!message || !message->text[0]) return;
    size_t max = sizeof(room_feed) / sizeof(room_feed[0]);
    if (room_feed_count < max) room_feed_count++;
    for (size_t i = room_feed_count - 1; i > 0; --i) room_feed[i] = room_feed[i - 1];
    room_feed[0] = *message;
    refresh_home_messages();
}

static void on_room_message(const cyd_room_message_t *message, bool live_event) {
    if (!cyd_lvgl_lock(250)) return;
    add_room_message_locked(message);
    bool remote = live_event && strcmp(message->sender_id, local_device_id) != 0;
    cyd_lvgl_unlock();
    if (remote) {
        cyd_bsp_play_tone(988, 85);
        cyd_bsp_rgb_set(false, true, true);
        vTaskDelay(pdMS_TO_TICKS(120));
        cyd_bsp_rgb_off();
    }
}

static void on_room_server_status(bool connected) {
    if (!cyd_lvgl_lock(100)) return;
    const char *text = connected ? "Raum online" : "Server offline";
    if (lbl_server_status) {
        lv_label_set_text(lbl_server_status, text);
        lv_obj_set_style_text_color(lbl_server_status,
            lv_color_hex(connected ? 0x49A879 : 0xA05A5A), 0);
    }
    cyd_lvgl_unlock();
}

static void create_demo_ui(void) {
    cyd_config_t cfg = {0};
    cyd_config_load(&cfg);
    snprintf(local_device_id, sizeof(local_device_id), "%s", cfg.device_id[0] ? cfg.device_id : "CYD_Display");

    home_screen = lv_obj_create(NULL);
    send_screen = lv_obj_create(NULL);
    set_screen_background(home_screen);
    set_screen_background(send_screen);
    lv_obj_add_event_cb(home_screen, home_gesture_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(send_screen, send_gesture_cb, LV_EVENT_GESTURE, NULL);
    add_header(home_screen, true);
    add_header(send_screen, false);

    lv_obj_t *room_label = lv_label_create(home_screen);
    char room_text[48];
    snprintf(room_text, sizeof(room_text), "Raum: %s", cfg.room_code[0] ? cfg.room_code : "DeskRoom-1");
    lv_label_set_text(room_label, room_text);
    lv_obj_set_style_text_color(room_label, lv_color_hex(0x8894A6), 0);
    lv_obj_align(room_label, LV_ALIGN_TOP_LEFT, 8, 31);

    lbl_server_status = lv_label_create(home_screen);
    lv_label_set_text(lbl_server_status, "Server verbindet …");
    lv_obj_set_style_text_color(lbl_server_status, lv_color_hex(0x596579), 0);
    lv_obj_align(lbl_server_status, LV_ALIGN_TOP_RIGHT, -8, 31);

    lv_obj_t *bubble = lv_obj_create(home_screen);
    lv_obj_set_size(bubble, 211, 123);
    lv_obj_set_pos(bubble, 8, 60);
    lv_obj_set_style_bg_color(bubble, lv_color_hex(0x121A26), 0);
    lv_obj_set_style_border_color(bubble, lv_color_hex(0x35445A), 0);
    lv_obj_set_style_radius(bubble, 12, 0);
    lv_obj_set_style_pad_all(bubble, 9, 0);

    home_sender = lv_label_create(bubble);
    lv_label_set_text(home_sender, "Willkommen im Raum");
    lv_obj_set_style_text_color(home_sender, lv_color_hex(0xD7AD54), 0);
    lv_obj_align(home_sender, LV_ALIGN_TOP_LEFT, 0, 0);

    home_message = lv_label_create(bubble);
    lv_label_set_text(home_message, "Noch keine Nachrichten.\nWische nach links zum Senden.");
    lv_label_set_long_mode(home_message, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(home_message, 190);
    lv_obj_set_style_text_color(home_message, lv_color_hex(0xE1E5EC), 0);
    lv_obj_align(home_message, LV_ALIGN_TOP_LEFT, 0, 28);

    home_avatar = lv_image_create(home_screen);
    lv_image_set_src(home_avatar, &cyd_avatar_cat);
    lv_obj_set_pos(home_avatar, 231, 83);

    home_emotion = lv_label_create(home_screen);
    lv_label_set_text(home_emotion, "...");
    lv_obj_set_style_text_color(home_emotion, lv_color_hex(0xD7AD54), 0);
    lv_obj_set_style_text_align(home_emotion, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(home_emotion, 56);
    lv_obj_align(home_emotion, LV_ALIGN_TOP_MID, 111, 142);

    home_previous = lv_label_create(home_screen);
    lv_label_set_text(home_previous, "Neue Nachrichten aus dem Raum erscheinen hier.");
    lv_label_set_long_mode(home_previous, LV_LABEL_LONG_DOT);
    lv_obj_set_width(home_previous, 302);
    lv_obj_set_style_text_color(home_previous, lv_color_hex(0x929CAF), 0);
    lv_obj_align(home_previous, LV_ALIGN_TOP_LEFT, 9, 190);

    lv_obj_t *home_send = lv_button_create(home_screen);
    lv_obj_set_size(home_send, 92, 25);
    lv_obj_align(home_send, LV_ALIGN_BOTTOM_RIGHT, -5, -3);
    set_dark_button_style(home_send, 0x182433, 0x50677D);
    lv_obj_add_event_cb(home_send, switch_to_send_screen, LV_EVENT_CLICKED, NULL);
    lv_obj_t *home_send_label = lv_label_create(home_send);
    lv_label_set_text(home_send_label, "Senden  >");
    lv_obj_center(home_send_label);

    const char *category_names[] = {"Stimmung", "Alltag", "Nähe", "Pause"};
    for (int i = 0; i < 4; ++i) {
        lv_obj_t *button = lv_button_create(send_screen);
        category_buttons[i] = button;
        lv_obj_set_size(button, 74, 24);
        lv_obj_set_pos(button, 6 + i * 78, 31);
        set_dark_button_style(button, 0x1C2B38, 0x607D8B);
        lv_obj_add_event_cb(button, category_button_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *label = lv_label_create(button);
        lv_label_set_text(label, category_names[i]);
        lv_obj_set_style_text_color(label, lv_color_hex(0xD5DAE3), 0);
        lv_obj_center(label);
    }

    send_grid = lv_obj_create(send_screen);
    lv_obj_remove_style_all(send_grid);
    lv_obj_set_size(send_grid, 312, 146);
    lv_obj_set_pos(send_grid, 4, 58);
    rebuild_preset_grid();

    lv_obj_t *back = lv_button_create(send_screen);
    lv_obj_set_size(back, 84, 23);
    lv_obj_align(back, LV_ALIGN_BOTTOM_LEFT, 5, -3);
    set_dark_button_style(back, 0x10151E, 0x334155);
    lv_obj_add_event_cb(back, switch_to_home_screen, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_label = lv_label_create(back);
    lv_label_set_text(back_label, "< Zuhause");
    lv_obj_center(back_label);

    lv_obj_t *set_hint = lv_label_create(send_screen);
    lv_label_set_text(set_hint, "Sets antippen: ein / aus");
    lv_obj_set_style_text_color(set_hint, lv_color_hex(0x778397), 0);
    lv_obj_align(set_hint, LV_ALIGN_BOTTOM_MID, 0, -7);

    lbl_toast = lv_label_create(home_screen);
    lv_label_set_text(lbl_toast, "Swipe < zum Senden");
    lv_obj_set_style_text_color(lbl_toast, lv_color_hex(0x596579), 0);
    lv_obj_align(lbl_toast, LV_ALIGN_BOTTOM_LEFT, 8, -8);

    lv_screen_load(home_screen);
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
    cyd_messages_init(on_room_message, on_room_server_status);
    cyd_wifi_connect_sta();

    ESP_LOGI(TAG, "CYD initialization completed.");
}
