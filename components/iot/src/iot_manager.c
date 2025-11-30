#include "iot_manager.h"
#include "mqtt_client.h"
#include "esp_https_ota.h"
#ifdef CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "core_service.h"

static const char *TAG = "IOT";
static esp_mqtt_client_handle_t mqtt_client = NULL;

// Default broker for testing
#define MQTT_BROKER_URI "mqtt://test.mosquitto.org"
#define MQTT_TOPIC_STATS "reptile/stats"

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT Connected");
        iot_mqtt_publish_stats();
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT Disconnected");
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT Error");
        break;
    default:
        break;
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
    return esp_mqtt_client_start(mqtt_client);
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