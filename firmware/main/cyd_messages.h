#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "cyd_presets.h"

typedef struct {
    char sender_id[32];
    char sender_name[32];
    char text[481];
    uint8_t avatar_id;
    char category[17];
    char emotion[17];
} cyd_room_message_t;

typedef void (*cyd_room_message_cb_t)(const cyd_room_message_t *message, bool live_event);
typedef void (*cyd_room_status_cb_t)(bool connected);

void cyd_messages_init(cyd_room_message_cb_t message_cb, cyd_room_status_cb_t status_cb);
esp_err_t cyd_messages_start(void);
bool cyd_messages_send_preset(const cyd_preset_t *preset);\n