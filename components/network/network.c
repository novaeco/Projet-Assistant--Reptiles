#include "network.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "nvs.h"
#include "ui.h"
#include "sdkconfig.h"

#define TAG "network"

static char ip_addr[16];
static char wifi_ssid[32];
static bool wifi_connected;
static bool wifi_dirty;
static bool ble_connected;
static bool ble_dirty;
static esp_ble_adv_params_t adv_params = {
    .adv_int_min = 0x20,
    .adv_int_max = 0x40,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            wifi_connected = false;
            ip_addr[0] = '\0';
            esp_wifi_connect();
            wifi_dirty = true;
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            wifi_connected = false;
            ip_addr[0] = '\0';
            esp_wifi_connect();
            wifi_dirty = true;
            ui_show_error(ui_get_str(UI_STR_WIFI_DISCONNECTED));
            break;
        default:
            break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        snprintf(ip_addr, sizeof(ip_addr), IPSTR, IP2STR(&event->ip_info.ip));
        wifi_connected = true;
        wifi_dirty = true;
    }
}

static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                                esp_ble_gatts_cb_param_t *param)
{
    switch (event) {
    case ESP_GATTS_CONNECT_EVT:
        ble_connected = true;
        ble_dirty = true;
        break;
    case ESP_GATTS_DISCONNECT_EVT:
        ble_connected = false;
        ble_dirty = true;
        esp_ble_gap_start_advertising(&adv_params);
        ui_show_error(ui_get_str(UI_STR_BLE_DISCONNECTED));
        break;
    default:
        break;
    }
}

esp_err_t network_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
        char msg[64];
        snprintf(msg, sizeof(msg), ui_get_str(UI_STR_NET_INIT_FAILED), esp_err_to_name(err));
        ui_show_error(msg);
        return err;
    }
    err = esp_netif_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(err));
        char msg[64];
        snprintf(msg, sizeof(msg), ui_get_str(UI_STR_NET_INIT_FAILED), esp_err_to_name(err));
        ui_show_error(msg);
        return err;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_event_loop_create_default failed: %s", esp_err_to_name(err));
        char msg[64];
        snprintf(msg, sizeof(msg), ui_get_str(UI_STR_NET_INIT_FAILED), esp_err_to_name(err));
        ui_show_error(msg);
        return err;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
        char msg[64];
        snprintf(msg, sizeof(msg), ui_get_str(UI_STR_NET_INIT_FAILED), esp_err_to_name(err));
        ui_show_error(msg);
        return err;
    }

    esp_netif_create_default_wifi_sta();

    char ssid[32] = CONFIG_WIFI_SSID;
    char pass[64] = CONFIG_WIFI_PASSWORD;
    nvs_handle_t nvs;
    if (nvs_open("wifi", NVS_READONLY, &nvs) == ESP_OK) {
        size_t len = sizeof(ssid);
        if (nvs_get_str(nvs, "ssid", ssid, &len) != ESP_OK) {
            strcpy(ssid, CONFIG_WIFI_SSID);
        }
        len = sizeof(pass);
        if (nvs_get_str(nvs, "pass", pass, &len) != ESP_OK) {
            strcpy(pass, CONFIG_WIFI_PASSWORD);
        }
        nvs_close(nvs);
    }

    strncpy(wifi_ssid, ssid, sizeof(wifi_ssid));
    wifi_ssid[sizeof(wifi_ssid) - 1] = '\0';

    wifi_config_t sta_cfg = {
        .sta = {
            .ssid = "",
            .password = "",
        }
    };
    strncpy((char *)sta_cfg.sta.ssid, ssid, sizeof(sta_cfg.sta.ssid));
    strncpy((char *)sta_cfg.sta.password, pass, sizeof(sta_cfg.sta.password));

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_mode failed: %s", esp_err_to_name(err));
        char msg[64];
        snprintf(msg, sizeof(msg), ui_get_str(UI_STR_NET_INIT_FAILED), esp_err_to_name(err));
        ui_show_error(msg);
        return err;
    }
    err = esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config failed: %s", esp_err_to_name(err));
        char msg[64];
        snprintf(msg, sizeof(msg), ui_get_str(UI_STR_NET_INIT_FAILED), esp_err_to_name(err));
        ui_show_error(msg);
        return err;
    }

    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              wifi_event_handler, NULL, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi handler register failed: %s", esp_err_to_name(err));
        char msg[64];
        snprintf(msg, sizeof(msg), ui_get_str(UI_STR_NET_INIT_FAILED), esp_err_to_name(err));
        ui_show_error(msg);
        return err;
    }
    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                              wifi_event_handler, NULL, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ip handler register failed: %s", esp_err_to_name(err));
        char msg[64];
        snprintf(msg, sizeof(msg), ui_get_str(UI_STR_NET_INIT_FAILED), esp_err_to_name(err));
        ui_show_error(msg);
        return err;
    }

    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(err));
        char msg[64];
        snprintf(msg, sizeof(msg), ui_get_str(UI_STR_NET_INIT_FAILED), esp_err_to_name(err));
        ui_show_error(msg);
        return err;
    }

    wifi_connected = false;
    ip_addr[0] = '\0';
    wifi_dirty = true;
    ble_connected = false;
    ble_dirty = true;

    err = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "bt mem release failed: %s", esp_err_to_name(err));
        char msg[64];
        snprintf(msg, sizeof(msg), ui_get_str(UI_STR_NET_INIT_FAILED), esp_err_to_name(err));
        ui_show_error(msg);
        return err;
    }
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    err = esp_bt_controller_init(&bt_cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "bt controller init failed: %s", esp_err_to_name(err));
        char msg[64];
        snprintf(msg, sizeof(msg), ui_get_str(UI_STR_NET_INIT_FAILED), esp_err_to_name(err));
        ui_show_error(msg);
        return err;
    }
    err = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "bt controller enable failed: %s", esp_err_to_name(err));
        char msg[64];
        snprintf(msg, sizeof(msg), ui_get_str(UI_STR_NET_INIT_FAILED), esp_err_to_name(err));
        ui_show_error(msg);
        return err;
    }

    err = esp_bluedroid_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "bluedroid init failed: %s", esp_err_to_name(err));
        char msg[64];
        snprintf(msg, sizeof(msg), ui_get_str(UI_STR_NET_INIT_FAILED), esp_err_to_name(err));
        ui_show_error(msg);
        return err;
    }
    err = esp_bluedroid_enable();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "bluedroid enable failed: %s", esp_err_to_name(err));
        char msg[64];
        snprintf(msg, sizeof(msg), ui_get_str(UI_STR_NET_INIT_FAILED), esp_err_to_name(err));
        ui_show_error(msg);
        return err;
    }

    esp_ble_gatts_register_callback(gatts_event_handler);
    esp_ble_gatts_app_register(0);
    esp_ble_gap_start_advertising(&adv_params);

    ESP_LOGI(TAG, "Wi-Fi 6 and BLE initialized");
    return ESP_OK;
}

void network_update(void)
{
    if (wifi_dirty || ble_dirty) {
        ui_update_network(wifi_ssid,
                          wifi_connected ? ip_addr : "N/A",
                          ble_connected);
        wifi_dirty = false;
        ble_dirty = false;
    }
}

esp_err_t network_save_credentials(const char *ssid, const char *pass)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("wifi", NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(nvs, "ssid", ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "pass", pass);
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

