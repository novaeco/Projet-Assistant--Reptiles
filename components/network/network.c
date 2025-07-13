#include "network.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "lvgl.h"

#define TAG "network"

static lv_obj_t *status_label;

void network_init(void)
{
    nvs_flash_init();
    esp_netif_init();
    esp_event_loop_create_default();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();

    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_bt_controller_init(&bt_cfg);
    esp_bt_controller_enable(ESP_BT_MODE_BLE);

    status_label = lv_label_create(lv_scr_act());
    lv_label_set_text(status_label, "Wi-Fi/BLE starting...");

    ESP_LOGI(TAG, "Wi-Fi 6 and BLE initialized");
}

void network_update(void)
{
    wifi_ap_record_t info;
    if (esp_wifi_sta_get_ap_info(&info) == ESP_OK) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Connected: %s", (char *)info.ssid);
        lv_label_set_text(status_label, buf);
    }
}

