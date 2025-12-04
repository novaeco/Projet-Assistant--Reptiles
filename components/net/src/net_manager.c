#include "net_manager.h"
#include "net_server.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "NET";
static bool is_connected = false;
static bool has_credentials = false;
static esp_timer_handle_t s_wifi_retry_timer = NULL;
static uint32_t s_wifi_backoff_ms = 1000; // start at 1s
static const char *DEFAULT_WIFI_SSID = "MY_SSID";
static const char *DEFAULT_WIFI_PASSWORD = "MY_PASS";

static esp_err_t apply_sta_config_with_recovery(const wifi_config_t *wifi_config_in)
{
    if (wifi_config_in == NULL) {
        ESP_LOGE(TAG, "Cannot apply STA config: null input");
        return ESP_ERR_INVALID_ARG;
    }

    wifi_mode_t current_mode = WIFI_MODE_MAX;
    esp_err_t err = esp_wifi_get_mode(&current_mode);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_get_mode failed: %s", esp_err_to_name(err));
        // Continue with WIFI_MODE_MAX in logs; recovery below will reset mode as needed
    }
    wifi_config_t cfg = *wifi_config_in;

    ESP_LOGI(TAG, "Applying STA config (mode=%d)", (int)current_mode);

    err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    esp_wifi_get_mode(&current_mode);
    wifi_config_t cfg = *wifi_config_in;

    ESP_LOGI(TAG, "Applying STA config (mode=%d)", (int)current_mode);

    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (err == ESP_ERR_WIFI_MODE) {
        ESP_LOGW(TAG, "esp_wifi_set_config returned ESP_ERR_WIFI_MODE (mode=%d)", (int)current_mode);

        esp_wifi_disconnect();
        esp_wifi_stop();

        err = esp_wifi_set_mode(WIFI_MODE_STA);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set STA mode during recovery: %s", esp_err_to_name(err));
            return err;
        }

        err = esp_wifi_start();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start Wi-Fi during recovery: %s", esp_err_to_name(err));
            return err;
        }

        err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to apply STA config: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "STA config applied successfully");

    esp_err_t connect_err = esp_wifi_connect();
    if (connect_err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_connect failed (non-blocking): %s", esp_err_to_name(connect_err));
    }

    return ESP_OK;
}

static void wifi_retry_cb(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Wi-Fi reconnect attempt (backoff %ums)", s_wifi_backoff_ms);
    esp_wifi_connect();
}

static void schedule_wifi_retry(uint32_t delay_ms)
{
    if (!has_credentials) {
        ESP_LOGW(TAG, "Wi-Fi credentials not set, waiting...");
        return;
    }

    if (!s_wifi_retry_timer) {
        const esp_timer_create_args_t tmr_args = {
            .callback = wifi_retry_cb,
            .name = "wifi_retry"
        };
        ESP_ERROR_CHECK(esp_timer_create(&tmr_args, &s_wifi_retry_timer));
    }

    ESP_ERROR_CHECK(esp_timer_stop(s_wifi_retry_timer));
    ESP_ERROR_CHECK(esp_timer_start_once(s_wifi_retry_timer, delay_ms * 1000ULL));
}

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Wi-Fi STA start");
        schedule_wifi_retry(10); // tiny delay to allow config to settle
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        is_connected = false;
        net_server_stop(); // Stop server on disconnect
        wifi_event_sta_disconnected_t *disc = (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGW(TAG, "Wi-Fi disconnected (reason=%d).", disc ? disc->reason : -1);
        // Exponential backoff capped at 60s
        s_wifi_backoff_ms = s_wifi_backoff_ms < 60000 ? s_wifi_backoff_ms * 2 : 60000;
        schedule_wifi_retry(s_wifi_backoff_ms);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR " (netmask=" IPSTR ", gw=" IPSTR ")",
                 IP2STR(&event->ip_info.ip), IP2STR(&event->ip_info.netmask), IP2STR(&event->ip_info.gw));
        is_connected = true;
        s_wifi_backoff_ms = 1000; // reset backoff after success
        wifi_ap_record_t ap_info = {0};
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            ESP_LOGI(TAG, "Connected SSID=%s RSSI=%d dBm", (char *)ap_info.ssid, ap_info.rssi);
        }
        net_server_start(); // Start server on connect
    }
}

esp_err_t net_init(void)
{
    ESP_LOGI(TAG, "Initializing Network...");
    
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    // Apply debug credentials so the station connects immediately when no NVS entries are present
    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, DEFAULT_WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, DEFAULT_WIFI_PASSWORD, sizeof(wifi_config.sta.password));
    ESP_LOGI(TAG, "Applying debug Wi-Fi credentials (SSID='%s')", DEFAULT_WIFI_SSID);
    if (apply_sta_config_with_recovery(&wifi_config) == ESP_OK) {
        has_credentials = (strlen(DEFAULT_WIFI_SSID) > 0);
    } else {
        has_credentials = false;
    }

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        NULL));

    ESP_LOGI(TAG, "Starting Wi-Fi station...");
    ESP_ERROR_CHECK(esp_wifi_start());

    // Init SNTP
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    ESP_LOGI(TAG, "SNTP client initialized (pool.ntp.org)");

    return ESP_OK;
}

esp_err_t net_connect(const char *ssid, const char *password)
{
    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));

    ESP_LOGI(TAG, "Connecting to %s...", ssid);
    if (apply_sta_config_with_recovery(&wifi_config) == ESP_OK) {
        has_credentials = true;
        s_wifi_backoff_ms = 1000;
        schedule_wifi_retry(10);
    }

    return ESP_OK;
}

bool net_is_connected(void)
{
    return is_connected;
}

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    // ... (Same as before, minimal implementation)
    return ESP_OK;
}

esp_err_t net_http_get(const char *url, char *out_buffer, size_t buffer_len)
{
    if (!is_connected) return ESP_FAIL;

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = _http_event_handler,
        .timeout_ms = 5000,
        .buffer_size = 1024,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int read_len = esp_http_client_read_response(client, out_buffer, buffer_len - 1);
        if (read_len >= 0) {
            out_buffer[read_len] = '\0';
        } else {
            err = ESP_FAIL;
        }
    }
    esp_http_client_cleanup(client);
    return err;
}