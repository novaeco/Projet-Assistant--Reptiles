#include "net_manager.h"
#include "net_server.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check_compat.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "NET";
static const char *NVS_NAMESPACE = "net";
static const char *NVS_KEY_WIFI_SSID = "wifi_ssid";
static const char *NVS_KEY_WIFI_PASS = "wifi_pass";

static bool is_connected = false;
static bool has_credentials = false;
static esp_timer_handle_t s_wifi_retry_timer = NULL;
static uint32_t s_wifi_backoff_ms = 1000; // start at 1s
static const uint32_t WIFI_RETRY_BACKOFF_MAX_MS = 30000;
static TaskHandle_t s_wifi_retry_task = NULL;

static esp_err_t apply_sta_config_with_recovery(const wifi_config_t *wifi_config_in)
{
    if (wifi_config_in == NULL) {
        ESP_LOGE(TAG, "Cannot apply STA config: null input");
        return ESP_ERR_INVALID_ARG;
    }

    wifi_mode_t current_mode = WIFI_MODE_NULL;
    esp_err_t err = esp_wifi_get_mode(&current_mode);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_get_mode failed: %s", esp_err_to_name(err));
        // Continue; recovery below will reset mode as needed
    }

    wifi_config_t cfg = *wifi_config_in;

    ESP_LOGI(TAG, "Applying STA config (mode=%d)", (int)current_mode);

    err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
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

    return ESP_OK;
}

static void wifi_retry_execute(void)
{
    ESP_LOGI(TAG, "Wi-Fi reconnect attempt (backoff %ums)", s_wifi_backoff_ms);
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_connect failed during retry: %s", esp_err_to_name(err));
    }
}

static void wifi_retry_task(void *arg)
{
    (void)arg;
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        wifi_retry_execute();
    }
}

static void wifi_retry_timer_cb(void *arg)
{
    (void)arg;
    if (s_wifi_retry_task) {
        xTaskNotifyGive(s_wifi_retry_task);
    }
}

static esp_err_t ensure_wifi_retry_timer(void)
{
    if (s_wifi_retry_timer) {
        return ESP_OK;
    }

    const esp_timer_create_args_t tmr_args = {
        .callback = wifi_retry_timer_cb,
        .name = "wifi_retry"
    };
    esp_err_t err = esp_timer_create(&tmr_args, &s_wifi_retry_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create Wi-Fi retry timer: %s", esp_err_to_name(err));
    }
    return err;
}

static esp_err_t ensure_wifi_retry_task(void)
{
    if (s_wifi_retry_task) {
        return ESP_OK;
    }

    BaseType_t task_ok = xTaskCreatePinnedToCore(wifi_retry_task, "wifi_retry_task", 4096, NULL, 4, &s_wifi_retry_task, 0);
    if (task_ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Wi-Fi retry task");
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void stop_wifi_retry_timer_best_effort(void)
{
    if (!s_wifi_retry_timer) {
        return;
    }

    esp_err_t err = esp_timer_stop(s_wifi_retry_timer);
    if (err == ESP_ERR_INVALID_STATE) {
        // Timer was not running; treat as expected.
        ESP_LOGD(TAG, "Wi-Fi retry timer was not running");
        return;
    }

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to stop Wi-Fi retry timer: %s", esp_err_to_name(err));
    }
}

static void schedule_wifi_retry(uint32_t delay_ms)
{
    if (!has_credentials) {
        ESP_LOGW(TAG, "Wi-Fi credentials not set, waiting...");
        return;
    }

    if (ensure_wifi_retry_task() != ESP_OK) {
        return;
    }

    if (ensure_wifi_retry_timer() != ESP_OK) {
        return;
    }

    stop_wifi_retry_timer_best_effort();

    esp_err_t err = esp_timer_start_once(s_wifi_retry_timer, delay_ms * 1000ULL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to schedule Wi-Fi retry: %s", esp_err_to_name(err));
    }
}

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Wi-Fi STA start");
        if (!has_credentials) {
            ESP_LOGW(TAG, "WiFi not provisioned, STA connect skipped");
            return;
        }
        esp_err_t err = esp_wifi_connect();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "esp_wifi_connect failed on STA start: %s", esp_err_to_name(err));
            schedule_wifi_retry(s_wifi_backoff_ms);
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        is_connected = false;
        net_server_stop(); // Stop server on disconnect
        wifi_event_sta_disconnected_t *disc = (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGW(TAG, "Wi-Fi disconnected (reason=%d).", disc ? disc->reason : -1);
        // Exponential backoff capped at 30s
        if (s_wifi_backoff_ms < WIFI_RETRY_BACKOFF_MAX_MS) {
            uint32_t next_backoff = s_wifi_backoff_ms * 2;
            s_wifi_backoff_ms = next_backoff > WIFI_RETRY_BACKOFF_MAX_MS ? WIFI_RETRY_BACKOFF_MAX_MS : next_backoff;
        }
        schedule_wifi_retry(s_wifi_backoff_ms);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR " (netmask=" IPSTR ", gw=" IPSTR ")",
                 IP2STR(&event->ip_info.ip), IP2STR(&event->ip_info.netmask), IP2STR(&event->ip_info.gw));
        is_connected = true;
        s_wifi_backoff_ms = 1000; // reset backoff after success
        stop_wifi_retry_timer_best_effort();
        wifi_ap_record_t ap_info = {0};
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            ESP_LOGI(TAG, "Connected SSID=%s RSSI=%d dBm", (char *)ap_info.ssid, ap_info.rssi);
        }
        net_server_start(); // Start server on connect
    }
}

static esp_err_t net_load_credentials_from_nvs(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    size_t ssid_required = 0;
    size_t pass_required = 0;
    err = nvs_get_str(nvs, NVS_KEY_WIFI_SSID, NULL, &ssid_required);
    if (err == ESP_OK && ssid_required > ssid_len) {
        err = ESP_ERR_NVS_INVALID_LENGTH;
    }
    if (err == ESP_OK) {
        err = nvs_get_str(nvs, NVS_KEY_WIFI_SSID, ssid, &ssid_required);
    }
    if (err == ESP_OK) {
        err = nvs_get_str(nvs, NVS_KEY_WIFI_PASS, NULL, &pass_required);
        if (err == ESP_OK && pass_required > pass_len) {
            err = ESP_ERR_NVS_INVALID_LENGTH;
        }
    }
    if (err == ESP_OK) {
        err = nvs_get_str(nvs, NVS_KEY_WIFI_PASS, pass, &pass_required);
    }

    nvs_close(nvs);
    if (err != ESP_OK) {
        if (ssid_len > 0) {
            ssid[0] = '\0';
        }
        if (pass_len > 0) {
            pass[0] = '\0';
        }
    }
    return err;
}

static bool net_has_nonempty_credentials(const wifi_config_t *cfg)
{
    return cfg && cfg->sta.ssid[0] != '\0';
}

esp_err_t net_manager_set_credentials(const char *ssid, const char *password, bool persist)
{
    if (!ssid || !password) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t ssid_len = strlen(ssid);
    size_t pass_len = strlen(password);
    if (ssid_len >= sizeof(((wifi_config_t *)0)->sta.ssid) ||
        pass_len >= sizeof(((wifi_config_t *)0)->sta.password)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (persist) {
        nvs_handle_t nvs = 0;
        esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
        if (err != ESP_OK) {
            return err;
        }
        err = nvs_set_str(nvs, NVS_KEY_WIFI_SSID, ssid);
        if (err == ESP_OK) {
            err = nvs_set_str(nvs, NVS_KEY_WIFI_PASS, password);
        }
        if (err == ESP_OK) {
            err = nvs_commit(nvs);
        }
        nvs_close(nvs);
        if (err != ESP_OK) {
            return err;
        }
    }

    wifi_config_t wifi_cfg = {0};
    strlcpy((char *)wifi_cfg.sta.ssid, ssid, sizeof(wifi_cfg.sta.ssid));
    strlcpy((char *)wifi_cfg.sta.password, password, sizeof(wifi_cfg.sta.password));

    esp_err_t err = apply_sta_config_with_recovery(&wifi_cfg);
    if (err == ESP_OK) {
        has_credentials = true;
        s_wifi_backoff_ms = 1000;
        schedule_wifi_retry(10);
    }
    return err;
}

static bool net_try_load_credentials(wifi_config_t *wifi_cfg)
{
    char ssid[sizeof(wifi_cfg->sta.ssid)] = {0};
    char pass[sizeof(wifi_cfg->sta.password)] = {0};

    esp_err_t err = net_load_credentials_from_nvs(ssid, sizeof(ssid), pass, sizeof(pass));
    if (err == ESP_OK && ssid[0] != '\0') {
        ESP_LOGI(TAG, "Using WiFi credentials from NVS (namespace '%s')", NVS_NAMESPACE);
        strlcpy((char *)wifi_cfg->sta.ssid, ssid, sizeof(wifi_cfg->sta.ssid));
        strlcpy((char *)wifi_cfg->sta.password, pass, sizeof(wifi_cfg->sta.password));
        return true;
    }

    if (strlen(CONFIG_NET_WIFI_SSID) > 0) {
        ESP_LOGI(TAG, "Using WiFi credentials from Kconfig (CONFIG_NET_WIFI_SSID)");
        strncpy((char *)wifi_cfg->sta.ssid, CONFIG_NET_WIFI_SSID, sizeof(wifi_cfg->sta.ssid));
        strncpy((char *)wifi_cfg->sta.password, CONFIG_NET_WIFI_PASS, sizeof(wifi_cfg->sta.password));
        return true;
    }

    return false;
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

    wifi_config_t wifi_config = {0};
    has_credentials = net_try_load_credentials(&wifi_config) && net_has_nonempty_credentials(&wifi_config);
    if (has_credentials) {
        ESP_LOGI(TAG, "Applying provisioned Wi-Fi credentials (SSID='%s')", (char *)wifi_config.sta.ssid);
        if (apply_sta_config_with_recovery(&wifi_config) != ESP_OK) {
            has_credentials = false;
        }
    } else {
        ESP_LOGW(TAG, "WiFi not provisioned, STA connect skipped");
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

    ESP_RETURN_ON_ERROR(ensure_wifi_retry_task(), TAG, "failed to start Wi-Fi retry worker");

    // Init SNTP
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    ESP_LOGI(TAG, "SNTP client initialized (pool.ntp.org)");

    return ESP_OK;
}

esp_err_t net_connect(const char *ssid, const char *password)
{
    ESP_LOGI(TAG, "Connecting to %s...", ssid);
    return net_manager_set_credentials(ssid, password, false);
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