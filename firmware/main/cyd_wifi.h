#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "cyd_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CYD_WIFI_STATE_DISCONNECTED,
    CYD_WIFI_STATE_CONNECTING,
    CYD_WIFI_STATE_CONNECTED,
    CYD_WIFI_STATE_AP_PORTAL,
} cyd_wifi_state_t;

typedef void (*cyd_wifi_state_cb_t)(cyd_wifi_state_t state, const char *info_str);

/**
 * @brief Initialize Wi-Fi subsystem and state callback
 */
esp_err_t cyd_wifi_init(cyd_wifi_state_cb_t cb);

/**
 * @brief Try connecting in Station (STA) mode with saved or provided credentials
 */
esp_err_t cyd_wifi_connect_sta(void);

/**
 * @brief Start SoftAP mode and embedded Webserver + Captive Portal DNS
 */
esp_err_t cyd_wifi_start_ap_portal(void);

/**
 * @brief Stop SoftAP mode & Webserver
 */
esp_err_t cyd_wifi_stop_ap_portal(void);

/**
 * @brief Get current Wi-Fi state
 */
cyd_wifi_state_t cyd_wifi_get_state(void);

/**
 * @brief Get local IP address as string
 */
const char* cyd_wifi_get_ip_str(void);

#ifdef __cplusplus
}
#endif
