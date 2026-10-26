#include "cyd_config.h"
#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "CYD_CONFIG";
#define NVS_NAMESPACE "cyd_cfg"

esp_err_t cyd_config_init(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing flash due to NVS structure change...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

esp_err_t cyd_config_load(cyd_config_t *cfg) {
    if (!cfg) return ESP_ERR_INVALID_ARG;
    memset(cfg, 0, sizeof(cyd_config_t));

    // Fallback defaults for Free Cloud Server
    strncpy(cfg->device_id, "CYD_Display", sizeof(cfg->device_id) - 1);
    strncpy(cfg->server_host, "deskconnect-hub.onrender.com", sizeof(cfg->server_host) - 1);
    cfg->server_port = 443; // Standard WSS / HTTPS port (Free Cloud Hosts)
    strncpy(cfg->room_code, "DeskRoom-1", sizeof(cfg->room_code) - 1);
    strncpy(cfg->partner_id, "all", sizeof(cfg->partner_id) - 1);

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No NVS configuration found, using defaults.");
        return ESP_ERR_NOT_FOUND;
    }

    size_t len = sizeof(cfg->wifi_ssid);
    nvs_get_str(handle, "ssid", cfg->wifi_ssid, &len);

    len = sizeof(cfg->wifi_pass);
    nvs_get_str(handle, "pass", cfg->wifi_pass, &len);

    len = sizeof(cfg->device_id);
    nvs_get_str(handle, "dev_id", cfg->device_id, &len);

    len = sizeof(cfg->server_host);
    nvs_get_str(handle, "srv_host", cfg->server_host, &len);

    uint16_t port = 0;
    if (nvs_get_u16(handle, "srv_port", &port) == ESP_OK && port > 0) {
        cfg->server_port = port;
    }

    len = sizeof(cfg->room_code);
    nvs_get_str(handle, "room", cfg->room_code, &len);

    len = sizeof(cfg->partner_id);
    nvs_get_str(handle, "partner", cfg->partner_id, &len);

    nvs_close(handle);

    ESP_LOGI(TAG, "Config loaded: SSID='%s', Dev='%s', Host='%s:%d', Room='%s'",
             cfg->wifi_ssid, cfg->device_id, cfg->server_host, cfg->server_port, cfg->room_code);

    if (strlen(cfg->wifi_ssid) == 0) {
        return ESP_ERR_NOT_FOUND;
    }

    return ESP_OK;
}

esp_err_t cyd_config_save(const cyd_config_t *cfg) {
    if (!cfg) return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    nvs_set_str(handle, "ssid", cfg->wifi_ssid);
    nvs_set_str(handle, "pass", cfg->wifi_pass);
    nvs_set_str(handle, "dev_id", cfg->device_id);
    nvs_set_str(handle, "srv_host", cfg->server_host);
    nvs_set_u16(handle, "srv_port", cfg->server_port);
    nvs_set_str(handle, "room", cfg->room_code);
    nvs_set_str(handle, "partner", cfg->partner_id);

    err = nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGI(TAG, "Configuration saved to NVS.");
    return err;
}

esp_err_t cyd_config_clear(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    nvs_erase_all(handle);
    nvs_commit(handle);
    nvs_close(handle);
    ESP_LOGI(TAG, "Configuration erased from NVS.");
    return ESP_OK;
}
