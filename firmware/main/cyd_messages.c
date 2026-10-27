#include "cyd_messages.h"

#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "cyd_config.h"

static const char *TAG = "CYD_MESSAGES";
#define WS_RX_BUFFER_SIZE 8192

static esp_websocket_client_handle_t ws_client;
static cyd_room_message_cb_t message_callback;
static cyd_room_status_cb_t status_callback;
static char ws_rx_buffer[WS_RX_BUFFER_SIZE];

static uint8_t avatar_for_device(const char *device_id) {
    uint32_t hash = 2166136261u;
    for (const unsigned char *p = (const unsigned char *)device_id; *p; ++p) {
        hash = (hash ^ *p) * 16777619u;
    }
    return (uint8_t)(hash & 1u);
}

static void copy_json_string(char *destination, size_t size, const cJSON *object, const char *key) {
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (cJSON_IsString(value) && value->valuestring) {
        snprintf(destination, size, "%s", value->valuestring);
    }
}

static void dispatch_message(const cJSON *item, bool live_event) {
    if (!message_callback || !cJSON_IsObject(item)) return;

    cyd_room_message_t message = {0};
    copy_json_string(message.sender_id, sizeof(message.sender_id), item, "sender_id");
    copy_json_string(message.sender_name, sizeof(message.sender_name), item, "sender_name");
    copy_json_string(message.text, sizeof(message.text), item, "text");
    copy_json_string(message.category, sizeof(message.category), item, "category");
    copy_json_string(message.emotion, sizeof(message.emotion), item, "emotion");
    if (message.sender_name[0] == '\0') {
        snprintf(message.sender_name, sizeof(message.sender_name), "%s", message.sender_id[0] ? message.sender_id : "Gast");
    }
    const cJSON *avatar = cJSON_GetObjectItemCaseSensitive(item, "avatar_id");
    if (cJSON_IsNumber(avatar)) message.avatar_id = (uint8_t)(avatar->valueint & 1);
    message_callback(&message, live_event);
}

static void dispatch_payload(const char *payload) {
    cJSON *root = cJSON_Parse(payload);
    if (!root) {
        ESP_LOGW(TAG, "Ignoring invalid JSON from room server.");
        return;
    }

    const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (cJSON_IsString(type) && strcmp(type->valuestring, "message") == 0) {
        dispatch_message(root, true);
    } else if (cJSON_IsString(type) && strcmp(type->valuestring, "history") == 0) {
        const cJSON *messages = cJSON_GetObjectItemCaseSensitive(root, "messages");
        const cJSON *item = NULL;
        cJSON_ArrayForEach(item, messages) {
            dispatch_message(item, false);
        }
    }
    cJSON_Delete(root);
}

static void ws_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    (void)handler_args;
    (void)base;
    esp_websocket_event_data_t *event = (esp_websocket_event_data_t *)event_data;

    if (event_id == WEBSOCKET_EVENT_CONNECTED) {
        ESP_LOGI(TAG, "Connected to room server.");
        if (status_callback) status_callback(true);

        cyd_config_t cfg;
        cyd_config_load(&cfg);
        cJSON *hello = cJSON_CreateObject();
        cJSON_AddStringToObject(hello, "type", "hello");
        cJSON_AddStringToObject(hello, "sender_name", cfg.device_id);
        cJSON_AddNumberToObject(hello, "avatar_id", avatar_for_device(cfg.device_id));
        char *json = cJSON_PrintUnformatted(hello);
        if (json) {
            esp_websocket_client_send_text(ws_client, json, strlen(json), pdMS_TO_TICKS(1000));
            cJSON_free(json);
        }
        cJSON_Delete(hello);
    } else if (event_id == WEBSOCKET_EVENT_DATA && event && event->op_code == 0x1) {
        if (event->payload_offset == 0) memset(ws_rx_buffer, 0, sizeof(ws_rx_buffer));
        if (event->payload_len <= 0 || event->payload_len >= WS_RX_BUFFER_SIZE ||
            event->payload_offset < 0 || event->data_len < 0 ||
            event->payload_offset + event->data_len >= WS_RX_BUFFER_SIZE) {
            ESP_LOGW(TAG, "Ignoring oversized WebSocket payload (%d bytes).", event->payload_len);
            return;
        }
        memcpy(ws_rx_buffer + event->payload_offset, event->data_ptr, event->data_len);
        if (event->payload_offset + event->data_len >= event->payload_len) {
            dispatch_payload(ws_rx_buffer);
        }
    } else if (event_id == WEBSOCKET_EVENT_DISCONNECTED || event_id == WEBSOCKET_EVENT_CLOSED) {
        ESP_LOGW(TAG, "Room server disconnected; reconnect will be attempted.");
        if (status_callback) status_callback(false);
    } else if (event_id == WEBSOCKET_EVENT_ERROR) {
        ESP_LOGW(TAG, "Room server transport error.");
        if (status_callback) status_callback(false);
    }
}

void cyd_messages_init(cyd_room_message_cb_t message_cb, cyd_room_status_cb_t connected_cb) {
    message_callback = message_cb;
    status_callback = connected_cb;
}

static void uri_encode(char *out, size_t out_size, const char *input) {
    static const char hex[] = "0123456789ABCDEF";
    size_t used = 0;
    for (const unsigned char *p = (const unsigned char *)input; *p && used + 4 < out_size; ++p) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' || *p == '.' || *p == '~') {
            out[used++] = (char)*p;
        } else {
            out[used++] = '%';
            out[used++] = hex[*p >> 4];
            out[used++] = hex[*p & 0x0F];
        }
    }
    out[used] = '\0';
}

esp_err_t cyd_messages_start(void) {
    if (ws_client) return ESP_OK;

    cyd_config_t cfg;
    esp_err_t err = cyd_config_load(&cfg);
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) return err;
    if (!cfg.server_host[0] || !cfg.room_code[0] || !cfg.device_id[0]) return ESP_ERR_INVALID_ARG;

    char room[96], device[96], uri[384];
    uri_encode(room, sizeof(room), cfg.room_code);
    uri_encode(device, sizeof(device), cfg.device_id);
    bool secure = cfg.server_port == 443;
    if (secure) {
        snprintf(uri, sizeof(uri), "wss://%s/ws/%s/%s", cfg.server_host, room, device);
    } else {
        snprintf(uri, sizeof(uri), "ws://%s:%u/ws/%s/%s", cfg.server_host, cfg.server_port, room, device);
    }

    const esp_websocket_client_config_t ws_cfg = {
        .uri = uri,
        .crt_bundle_attach = secure ? esp_crt_bundle_attach : NULL,
        .buffer_size = WS_RX_BUFFER_SIZE,
        .network_timeout_ms = 10000,
        .reconnect_timeout_ms = 5000,
        .ping_interval_sec = 25,
    };
    ws_client = esp_websocket_client_init(&ws_cfg);
    if (!ws_client) return ESP_ERR_NO_MEM;
    ESP_ERROR_CHECK(esp_websocket_register_events(ws_client, WEBSOCKET_EVENT_ANY, ws_event_handler, NULL));
    ESP_LOGI(TAG, "Connecting to %s", uri);
    err = esp_websocket_client_start(ws_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not start room WebSocket: %s", esp_err_to_name(err));
        ws_client = NULL;
    }
    return err;
}

static const char *category_for_preset(const char *id) {
    if (strcmp(id, "alles_gut") == 0 || strcmp(id, "schlafen") == 0) return "stimmung";
    if (strcmp(id, "hab_dich_lieb") == 0 || strcmp(id, "feierabend") == 0) return "naehe";
    if (strcmp(id, "beschaeftigt") == 0 || strcmp(id, "gleich_da") == 0) return "pause";
    return "alltag";
}

static const char *emotion_for_preset(const char *id) {
    if (strcmp(id, "alles_gut") == 0 || strcmp(id, "feierabend") == 0) return "happy";
    if (strcmp(id, "schlafen") == 0) return "sleepy";
    if (strcmp(id, "hunger") == 0) return "hungry";
    if (strcmp(id, "kaffee") == 0) return "curious";
    if (strcmp(id, "hab_dich_lieb") == 0) return "love";
    if (strcmp(id, "beschaeftigt") == 0) return "busy";
    if (strcmp(id, "gleich_da") == 0) return "wave";
    return "neutral";
}

bool cyd_messages_send_preset(const cyd_preset_t *preset) {
    if (!preset || !ws_client || !esp_websocket_client_is_connected(ws_client)) return false;

    cyd_config_t cfg;
    if (cyd_config_load(&cfg) != ESP_OK && cfg.device_id[0] == '\0') return false;
    cJSON *message = cJSON_CreateObject();
    if (!message) return false;
    cJSON_AddStringToObject(message, "type", "message");
    cJSON_AddStringToObject(message, "preset_id", preset->id);
    cJSON_AddStringToObject(message, "category", category_for_preset(preset->id));
    cJSON_AddStringToObject(message, "emotion", emotion_for_preset(preset->id));
    cJSON_AddStringToObject(message, "text", preset->message);
    char *json = cJSON_PrintUnformatted(message);
    bool sent = false;
    if (json) {
        int size = strlen(json);
        int written = esp_websocket_client_send_text(ws_client, json, size, pdMS_TO_TICKS(100));
        sent = written == size;
        cJSON_free(json);
    }
    cJSON_Delete(message);
    return sent;
}\n