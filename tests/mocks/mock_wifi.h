#pragma once
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "lvgl.h"

typedef struct lv_obj_t {
    int dummy;
} lv_obj_t;

extern esp_err_t mock_nvs_flash_init_ret;
extern esp_err_t mock_esp_netif_init_ret;
extern esp_err_t mock_esp_event_loop_create_default_ret;
extern esp_err_t mock_esp_wifi_init_ret;
extern esp_err_t mock_esp_wifi_set_mode_ret;
extern esp_err_t mock_esp_wifi_set_config_ret;
extern esp_err_t mock_esp_event_handler_instance_register_ret;
extern esp_err_t mock_esp_wifi_start_ret;
extern esp_err_t mock_esp_bt_controller_mem_release_ret;
extern esp_err_t mock_esp_bt_controller_init_ret;
extern esp_err_t mock_esp_bt_controller_enable_ret;
extern esp_err_t mock_esp_bluedroid_init_ret;
extern esp_err_t mock_esp_bluedroid_enable_ret;

void * lv_scr_act(void);
lv_obj_t * lv_label_create(const void *parent);
void lv_label_set_text(lv_obj_t *obj, const char *text);

esp_err_t nvs_flash_init(void);
esp_err_t esp_netif_init(void);
esp_err_t esp_event_loop_create_default(void);
esp_err_t esp_wifi_init(const wifi_init_config_t *cfg);
esp_netif_t * esp_netif_create_default_wifi_sta(void);
esp_err_t esp_wifi_set_mode(wifi_mode_t mode);
esp_err_t esp_wifi_set_config(wifi_interface_t iface, const wifi_config_t *cfg);
esp_err_t esp_event_handler_instance_register(esp_event_base_t event_base, int32_t event_id, esp_event_handler_t event_handler, void *event_handler_arg, esp_event_handler_instance_t *instance_out);
esp_err_t esp_wifi_start(void);
esp_err_t esp_bt_controller_mem_release(esp_bt_mode_t mode);
esp_err_t esp_bt_controller_init(const esp_bt_controller_config_t *cfg);
esp_err_t esp_bt_controller_enable(esp_bt_mode_t mode);
esp_err_t esp_bluedroid_init(void);
esp_err_t esp_bluedroid_enable(void);
esp_err_t esp_ble_gatts_register_callback(esp_gatts_cb_t callback);
esp_err_t esp_ble_gatts_app_register(uint16_t app_id);
esp_err_t esp_ble_gap_start_advertising(const esp_ble_adv_params_t *params);

typedef int32_t nvs_handle_t;
typedef enum {
    NVS_READONLY,
    NVS_READWRITE
} nvs_open_mode_t;
esp_err_t nvs_open(const char *name, nvs_open_mode_t open_mode, nvs_handle_t *out_handle);
esp_err_t nvs_get_str(nvs_handle_t handle, const char *key, char *out_value, size_t *length);
esp_err_t nvs_set_str(nvs_handle_t handle, const char *key, const char *value);
esp_err_t nvs_get_u8(nvs_handle_t handle, const char *key, uint8_t *out_value);
esp_err_t nvs_set_u8(nvs_handle_t handle, const char *key, uint8_t value);
esp_err_t nvs_commit(nvs_handle_t handle);
void nvs_close(nvs_handle_t handle);

