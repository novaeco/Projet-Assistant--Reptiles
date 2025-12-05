#include "iot_manager.h"
#include "mqtt_client.h"
#include "esp_https_ota.h"
#ifdef CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "cJSON.h"
#include "core_service.h"
#include "net_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "IOT";
static esp_mqtt_client_handle_t mqtt_client = NULL;
static bool mqtt_started = false;
static esp_timer_handle_t s_mqtt_retry_timer = NULL;
static uint32_t s_mqtt_backoff_ms = 1000;
static TaskHandle_t s_mqtt_retry_task = NULL;

// Default broker for testing
#define MQTT_BROKER_URI "mqtt://test.mosquitto.org"
#define MQTT_TOPIC_STATS "reptile/stats"

static void mqtt_schedule_start(uint32_t delay_ms);

static void mqtt_start_attempt(void)
{
    if (!mqtt_client) {
        return;
    }
    if (!net_is_connected()) {
        ESP_LOGW(TAG, "MQTT start postponed: Wi-Fi not ready");
        mqtt_schedule_start(s_mqtt_backoff_ms);
        return;
    }

    esp_err_t err = esp_mqtt_client_start(mqtt_client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "MQTT client started (%s)", MQTT_BROKER_URI);
        mqtt_started = true;
        s_mqtt_backoff_ms = 1000;
    } else {
        ESP_LOGW(TAG, "MQTT start failed: %s", esp_err_to_name(err));
        s_mqtt_backoff_ms = s_mqtt_backoff_ms < 60000 ? s_mqtt_backoff_ms * 2 : 60000;
        mqtt_schedule_start(s_mqtt_backoff_ms);
    }
}

static void mqtt_retry_task(void *arg)
{
    (void)arg;
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        mqtt_start_attempt();
    }
}

static void mqtt_retry_timer_cb(void *arg)
{
    (void)arg;
    if (s_mqtt_retry_task) {
        xTaskNotifyGive(s_mqtt_retry_task);
    }
}

static void mqtt_schedule_start(uint32_t delay_ms)
{
    if (!net_is_connected()) {
        ESP_LOGW(TAG, "MQTT deferred: Wi-Fi not connected yet");
        return;
    }

    if (!s_mqtt_retry_timer) {
        const esp_timer_create_args_t tmr_args = {
            .callback = mqtt_retry_timer_cb,
            .name = "mqtt_retry",
        };
        ESP_ERROR_CHECK(esp_timer_create(&tmr_args, &s_mqtt_retry_timer));
    }

    ESP_ERROR_CHECK(esp_timer_stop(s_mqtt_retry_timer));
    ESP_ERROR_CHECK(esp_timer_start_once(s_mqtt_retry_timer, delay_ms * 1000ULL));
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)event_data;
    (void)handler_args;
    (void)base;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT Connected");
        mqtt_started = true;
        s_mqtt_backoff_ms = 1000;
        iot_mqtt_publish_stats();
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT Disconnected");
        mqtt_started = false;
        s_mqtt_backoff_ms = s_mqtt_backoff_ms < 60000 ? s_mqtt_backoff_ms * 2 : 60000;
        mqtt_schedule_start(s_mqtt_backoff_ms);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT Error");
        break;
    default:
        break;
    }
}

static void ip_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)event_data;

    if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "MQTT notified of IP acquisition, starting client");
        s_mqtt_backoff_ms = 1000;
        mqtt_schedule_start(10);
    }
}

static void wifi_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)event_data;

    if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (mqtt_started && mqtt_client) {
            ESP_LOGI(TAG, "Stopping MQTT due to Wi-Fi disconnect");
            esp_mqtt_client_stop(mqtt_client);
            mqtt_started = false;
        }
    }
}

esp_err_t iot_init(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
    };

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (!mqtt_client) return ESP_FAIL;

    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    ESP_LOGI(TAG, "MQTT client configured for %s (waiting for IP)", MQTT_BROKER_URI);

    // Gate MQTT start on IP acquisition
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, wifi_event_handler, NULL, NULL));

    if (!s_mqtt_retry_task) {
        BaseType_t task_ok = xTaskCreatePinnedToCore(mqtt_retry_task, "mqtt_retry_task", 4096, NULL, 4, &s_mqtt_retry_task, 1);
        if (task_ok != pdPASS) {
            ESP_LOGE(TAG, "Failed to create MQTT retry task");
            return ESP_FAIL;
        }
    }

    // Start once network is up
    mqtt_schedule_start(10);
    return ESP_OK;
}

void iot_mqtt_publish_stats(void)
{
    if (!mqtt_client) return;

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "uptime", esp_timer_get_time() / 1000000);
    cJSON_AddNumberToObject(root, "free_heap", esp_get_free_heap_size());
    
    // Get animal count
    animal_summary_t *list = NULL;
    size_t count = 0;
    if (core_list_animals(&list, &count) == ESP_OK) {
        cJSON_AddNumberToObject(root, "animal_count", count);
        core_free_animal_list(list);
    }

    char *json_str = cJSON_PrintUnformatted(root);
    esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC_STATS, json_str, 0, 1, 0);
    
    free(json_str);
    cJSON_Delete(root);
    ESP_LOGI(TAG, "Stats published");
}

esp_err_t iot_ota_start(const char *url)
{
    ESP_LOGI(TAG, "Starting OTA from %s", url);

    esp_http_client_config_t http_cfg = {
        .url = url,
        .timeout_ms = 10000,
        .keep_alive_enable = true,
        .skip_cert_common_name_check = true,
#ifdef CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
        .crt_bundle_attach = esp_crt_bundle_attach,
#endif
    };

    const esp_https_ota_config_t ota_cfg = {
        .http_config = &http_cfg,
    };

    esp_err_t ret = esp_https_ota(&ota_cfg);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "OTA Successful, restarting...");
        esp_restart();
    } else {
        ESP_LOGE(TAG, "OTA Failed");
    }
    return ret;
}