#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char wifi_ssid[33];
    char wifi_pass[64];
    char device_id[32];      // e.g. "Buero" or "Wohnzimmer"
    char server_host[128];   // IP, DynDNS, Domain or Cloud Broker (e.g. "cyd.domain.de" / "broker.hivemq.com")
    uint16_t server_port;    // e.g. 443 (WSS), 8883 (MQTT TLS), 8000
    char room_code[32];      // Shared Pairing-Code / Room-ID (e.g. "SecretRoom123")
    char partner_id[32];     // Target CYD name or "all"
} cyd_config_t;

/**
 * @brief Initialize NVS flash
 */
esp_err_t cyd_config_init(void);

/**
 * @brief Load configuration from NVS. If keys don't exist, defaults are used.
 * @return ESP_OK if valid Wi-Fi credentials exist, ESP_ERR_NOT_FOUND otherwise.
 */
esp_err_t cyd_config_load(cyd_config_t *cfg);

/**
 * @brief Save configuration to NVS
 */
esp_err_t cyd_config_save(const cyd_config_t *cfg);

/**
 * @brief Clear saved configuration (Factory Reset)
 */
esp_err_t cyd_config_clear(void);

#ifdef __cplusplus
}
#endif
