#include "cyd_wifi.h"
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"

static const char *TAG = "CYD_WIFI";

#define MAX_STA_RETRIES       5
#define AP_SSID_PREFIX        "DeskConnect-"
#define AP_DEFAULT_PASS       ""  // Open hotspot for easy initial phone connection
#define AP_CHANNEL            1
#define AP_MAX_CONN           4

static cyd_wifi_state_t current_state = CYD_WIFI_STATE_DISCONNECTED;
static cyd_wifi_state_cb_t state_callback = NULL;
static esp_netif_t *sta_netif = NULL;
static esp_netif_t *ap_netif = NULL;
static httpd_handle_t webserver_handle = NULL;
static TaskHandle_t dns_task_handle = NULL;
static int sta_retry_num = 0;
static char current_ip[16] = "0.0.0.0";
static char ap_ssid_str[32] = "DeskConnect-Setup";

static void set_state(cyd_wifi_state_t st, const char *info) {
    current_state = st;
    if (state_callback) {
        state_callback(st, info);
    }
}

cyd_wifi_state_t cyd_wifi_get_state(void) {
    return current_state;
}

const char* cyd_wifi_get_ip_str(void) {
    return current_ip;
}

// ==========================================
// Captive Portal DNS Server (UDP Port 53)
// ==========================================
static void dns_server_task(void *pvParameters) {
    uint8_t rx_buffer[256];
    uint8_t tx_buffer[256];
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Unable to create DNS socket");
        vTaskDelete(NULL);
        return;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(53);

    if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "DNS socket bind failed");
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Captive portal DNS server listening on UDP :53...");

    while (1) {
        int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer), 0, (struct sockaddr *)&client_addr, &client_addr_len);
        if (len < 12) continue; // Minimum DNS header size

        // Prepare response
        memcpy(tx_buffer, rx_buffer, len);
        // Set Response Flags: standard response, no error, authoritative
        tx_buffer[2] = 0x81;
        tx_buffer[3] = 0x80;
        // Answer Count = 1
        tx_buffer[6] = 0x00;
        tx_buffer[7] = 0x01;

        // Append A record answer pointing to 192.168.4.1
        int idx = len;
        tx_buffer[idx++] = 0xc0; tx_buffer[idx++] = 0x0c; // Pointer to query name
        tx_buffer[idx++] = 0x00; tx_buffer[idx++] = 0x01; // Type A
        tx_buffer[idx++] = 0x00; tx_buffer[idx++] = 0x01; // Class IN
        tx_buffer[idx++] = 0x00; tx_buffer[idx++] = 0x00;
        tx_buffer[idx++] = 0x00; tx_buffer[idx++] = 0x3c; // TTL: 60s
        tx_buffer[idx++] = 0x00; tx_buffer[idx++] = 0x04; // Data length: 4 bytes
        tx_buffer[idx++] = 192;  tx_buffer[idx++] = 168;  // IP: 192.168.4.1
        tx_buffer[idx++] = 4;    tx_buffer[idx++] = 1;

        sendto(sock, tx_buffer, idx, 0, (struct sockaddr *)&client_addr, client_addr_len);
    }
}

// ==========================================
// Embedded Webserver (Setup Portal)
// ==========================================
static void url_decode(char *dst, const char *src, size_t dst_len) {
    size_t i = 0, j = 0;
    while (src[i] && j + 1 < dst_len) {
        if (src[i] == '%' && src[i+1] && src[i+2]) {
            char hex[3] = { src[i+1], src[i+2], 0 };
            dst[j++] = (char)strtol(hex, NULL, 16);
            i += 3;
        } else if (src[i] == '+') {
            dst[j++] = ' ';
            i++;
        } else {
            dst[j++] = src[i++];
        }
    }
    dst[j] = 0;
}

static esp_err_t root_get_handler(httpd_req_t *req) {
    cyd_config_t cfg;
    cyd_config_load(&cfg);

    char html_buf[2500];
    snprintf(html_buf, sizeof(html_buf),
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>DeskConnect Setup</title>"
        "<style>"
        "body{font-family:sans-serif;background:#181824;color:#eee;padding:15px;max-width:400px;margin:auto;}"
        "h2{color:#ffcc00;text-align:center;margin-bottom:5px;}"
        ".subtitle{color:#888;font-size:0.85em;text-align:center;margin-bottom:15px;}"
        ".card{background:#252538;border-radius:10px;padding:15px;margin-bottom:15px;}"
        "label{display:block;margin-top:10px;color:#aaa;font-size:0.85em;font-weight:bold;}"
        "input,select,button{width:100%%;box-sizing:border-box;padding:10px;margin-top:4px;border-radius:6px;border:1px solid #444;background:#181824;color:#fff;font-size:1em;}"
        "button{background:#ffcc00;color:#000;font-weight:bold;margin-top:15px;cursor:pointer;border:none;padding:12px;}"
        ".hint{font-size:0.75em;color:#ffcc00;margin-top:3px;}"
        "</style></head><body>"
        "<h2>📟 DeskConnect Setup</h2>"
        "<div class='subtitle'>WLAN & Vernetzung konfigurieren</div>"
        "<div class='card'>"
        "<form method='POST' action='/save'>"
        "<label>WLAN Netzwerk (SSID):</label>"
        "<input type='text' name='ssid' value='%s' required>"
        "<label>WLAN Passwort:</label>"
        "<input type='password' name='pass' value='%s'>"
        "<hr style='border:0;border-top:1px solid #333;margin:15px 0;'>"
        "<label>🔑 Gemeinsamer Raum-Code (Pairing-PIN):</label>"
        "<input type='text' name='room' value='%s' placeholder='z.B. Familie-2026' required>"
        "<div class='hint'>Gleichen Raum-Code auf beiden Displays eintragen für weltweite Kopplung!</div>"
        "<label>Gerätename / Standort:</label>"
        "<input type='text' name='dev_id' value='%s' placeholder='z.B. Buero oder Wohnzimmer' required>"
        "<label>Server / Broker Hostname:</label>"
        "<input type='text' name='srv_host' value='%s' placeholder='z.B. broker.hivemq.com oder IP' required>"
        "<label>Server Port:</label>"
        "<input type='number' name='srv_port' value='%d' required>"
        "<button type='submit'>💾 Speichern & Verbinden</button>"
        "</form></div></body></html>",
        cfg.wifi_ssid, cfg.wifi_pass, cfg.room_code, cfg.device_id, cfg.server_host, cfg.server_port
    );

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html_buf, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t save_post_handler(httpd_req_t *req) {
    char buf[512] = {0};
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    cyd_config_t new_cfg;
    memset(&new_cfg, 0, sizeof(new_cfg));
    new_cfg.server_port = 1883;

    // Parse URL-encoded body
    char *token = strtok(buf, "&");
    while (token != NULL) {
        char *eq = strchr(token, '=');
        if (eq) {
            *eq = '\0';
            char *key = token;
            char *val = eq + 1;
            char decoded[128];
            url_decode(decoded, val, sizeof(decoded));

            if (strcmp(key, "ssid") == 0) strncpy(new_cfg.wifi_ssid, decoded, sizeof(new_cfg.wifi_ssid) - 1);
            else if (strcmp(key, "pass") == 0) strncpy(new_cfg.wifi_pass, decoded, sizeof(new_cfg.wifi_pass) - 1);
            else if (strcmp(key, "dev_id") == 0) strncpy(new_cfg.device_id, decoded, sizeof(new_cfg.device_id) - 1);
            else if (strcmp(key, "srv_host") == 0) strncpy(new_cfg.server_host, decoded, sizeof(new_cfg.server_host) - 1);
            else if (strcmp(key, "srv_port") == 0) new_cfg.server_port = atoi(decoded);
            else if (strcmp(key, "room") == 0) strncpy(new_cfg.room_code, decoded, sizeof(new_cfg.room_code) - 1);
        }
        token = strtok(NULL, "&");
    }

    cyd_config_save(&new_cfg);

    const char *resp = "<!DOCTYPE html><html><head><meta charset='utf-8'>"
                       "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                       "<style>body{background:#181824;color:#ffcc00;text-align:center;font-family:sans-serif;padding:30px;}</style>"
                       "</head><body><h2>✅ Gespeichert!</h2><p>Display verbindet sich jetzt mit WLAN...</p></body></html>";
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);

    // Schedule reboot / reconnect in 1.5 seconds
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
    return ESP_OK;
}

// Redirect all 404s to root for Captive Portal detection
static esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static void start_webserver(void) {
    if (webserver_handle != NULL) return;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 4;
    config.lru_purge_enable = true;

    if (httpd_start(&webserver_handle, &config) == ESP_OK) {
        httpd_uri_t root_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = root_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(webserver_handle, &root_uri);

        httpd_uri_t save_uri = {
            .uri = "/save",
            .method = HTTP_POST,
            .handler = save_post_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(webserver_handle, &save_uri);

        httpd_register_err_handler(webserver_handle, HTTPD_404_NOT_FOUND, http_404_error_handler);
        ESP_LOGI(TAG, "Setup Webserver running on port 80.");
    }
}

static void stop_webserver(void) {
    if (webserver_handle) {
        httpd_stop(webserver_handle);
        webserver_handle = NULL;
    }
}

// ==========================================
// Wi-Fi Event Handler
// ==========================================
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Wi-Fi STA started, connecting...");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (sta_retry_num < MAX_STA_RETRIES) {
            sta_retry_num++;
            ESP_LOGW(TAG, "Retry connecting to Wi-Fi (%d/%d)...", sta_retry_num, MAX_STA_RETRIES);
            esp_wifi_connect();
        } else {
            ESP_LOGE(TAG, "Failed to connect to Wi-Fi. Starting AP portal fallback...");
            cyd_wifi_start_ap_portal();
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        sta_retry_num = 0;
        esp_ip4addr_ntoa(&event->ip_info.ip, current_ip, sizeof(current_ip));
        ESP_LOGI(TAG, "Connected! IP: %s", current_ip);
        set_state(CYD_WIFI_STATE_CONNECTED, current_ip);
    }
}

// ==========================================
// Public Control API
// ==========================================
esp_err_t cyd_wifi_init(cyd_wifi_state_cb_t cb) {
    state_callback = cb;
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    sta_netif = esp_netif_create_default_wifi_sta();
    ap_netif = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    // Determine unique AP SSID based on MAC address
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(ap_ssid_str, sizeof(ap_ssid_str), "%s%02X%02X", AP_SSID_PREFIX, mac[4], mac[5]);

    return ESP_OK;
}

esp_err_t cyd_wifi_connect_sta(void) {
    cyd_config_t cfg;
    if (cyd_config_load(&cfg) != ESP_OK || strlen(cfg.wifi_ssid) == 0) {
        ESP_LOGW(TAG, "No valid Wi-Fi credentials in NVS. Starting AP portal...");
        return cyd_wifi_start_ap_portal();
    }

    ESP_LOGI(TAG, "Attempting connection to SSID: %s", cfg.wifi_ssid);
    sta_retry_num = 0;
    set_state(CYD_WIFI_STATE_CONNECTING, cfg.wifi_ssid);

    wifi_config_t wifi_cfg = {0};
    strncpy((char *)wifi_cfg.sta.ssid, cfg.wifi_ssid, sizeof(wifi_cfg.sta.ssid));
    strncpy((char *)wifi_cfg.sta.password, cfg.wifi_pass, sizeof(wifi_cfg.sta.password));
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    return ESP_OK;
}

esp_err_t cyd_wifi_start_ap_portal(void) {
    ESP_LOGI(TAG, "Starting SoftAP: %s...", ap_ssid_str);
    set_state(CYD_WIFI_STATE_AP_PORTAL, ap_ssid_str);

    wifi_config_t wifi_ap_cfg = {
        .ap = {
            .channel = AP_CHANNEL,
            .max_connection = AP_MAX_CONN,
            .authmode = WIFI_AUTH_OPEN,
        },
    };
    strncpy((char *)wifi_ap_cfg.ap.ssid, ap_ssid_str, sizeof(wifi_ap_cfg.ap.ssid));

    ESP_ERROR_CHECK(esp_wifi_stop());
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Start Webserver
    start_webserver();

    // Start Captive Portal DNS Server Task if not already running
    if (dns_task_handle == NULL) {
        xTaskCreate(dns_server_task, "dns_task", 3072, NULL, 5, &dns_task_handle);
    }

    return ESP_OK;
}

esp_err_t cyd_wifi_stop_ap_portal(void) {
    stop_webserver();
    if (dns_task_handle) {
        vTaskDelete(dns_task_handle);
        dns_task_handle = NULL;
    }
    return ESP_OK;
}
