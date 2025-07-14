#include "mock_wifi.h"

esp_err_t mock_nvs_flash_init_ret = ESP_OK;
esp_err_t mock_esp_netif_init_ret = ESP_OK;
esp_err_t mock_esp_event_loop_create_default_ret = ESP_OK;
esp_err_t mock_esp_wifi_init_ret = ESP_OK;
esp_err_t mock_esp_wifi_set_mode_ret = ESP_OK;
esp_err_t mock_esp_wifi_set_config_ret = ESP_OK;
esp_err_t mock_esp_event_handler_instance_register_ret = ESP_OK;
esp_err_t mock_esp_wifi_start_ret = ESP_OK;
esp_err_t mock_esp_bt_controller_mem_release_ret = ESP_OK;
esp_err_t mock_esp_bt_controller_init_ret = ESP_OK;
esp_err_t mock_esp_bt_controller_enable_ret = ESP_OK;
esp_err_t mock_esp_bluedroid_init_ret = ESP_OK;
esp_err_t mock_esp_bluedroid_enable_ret = ESP_OK;

static lv_obj_t dummy;

void * lv_scr_act(void)
{
    return NULL;
}

lv_obj_t * lv_label_create(const void *parent)
{
    (void)parent;
    return &dummy;
}

void lv_label_set_text(lv_obj_t *obj, const char *text)
{
    (void)obj; (void)text;
}

esp_err_t nvs_flash_init(void) { return mock_nvs_flash_init_ret; }
esp_err_t esp_netif_init(void) { return mock_esp_netif_init_ret; }
esp_err_t esp_event_loop_create_default(void) { return mock_esp_event_loop_create_default_ret; }
esp_err_t esp_wifi_init(const wifi_init_config_t *cfg) { (void)cfg; return mock_esp_wifi_init_ret; }
esp_netif_t * esp_netif_create_default_wifi_sta(void) { return (esp_netif_t*)0x1; }
esp_err_t esp_wifi_set_mode(wifi_mode_t mode) { (void)mode; return mock_esp_wifi_set_mode_ret; }
esp_err_t esp_wifi_set_config(wifi_interface_t iface, const wifi_config_t *cfg) { (void)iface; (void)cfg; return mock_esp_wifi_set_config_ret; }
esp_err_t esp_event_handler_instance_register(esp_event_base_t event_base, int32_t event_id, esp_event_handler_t event_handler, void *event_handler_arg, esp_event_handler_instance_t *instance_out)
{
    (void)event_base; (void)event_id; (void)event_handler; (void)event_handler_arg; (void)instance_out;
    return mock_esp_event_handler_instance_register_ret;
}
esp_err_t esp_wifi_start(void) { return mock_esp_wifi_start_ret; }
esp_err_t esp_bt_controller_mem_release(esp_bt_mode_t mode) { (void)mode; return mock_esp_bt_controller_mem_release_ret; }
esp_err_t esp_bt_controller_init(const esp_bt_controller_config_t *cfg) { (void)cfg; return mock_esp_bt_controller_init_ret; }
esp_err_t esp_bt_controller_enable(esp_bt_mode_t mode) { (void)mode; return mock_esp_bt_controller_enable_ret; }
esp_err_t esp_bluedroid_init(void) { return mock_esp_bluedroid_init_ret; }
esp_err_t esp_bluedroid_enable(void) { return mock_esp_bluedroid_enable_ret; }
esp_err_t esp_ble_gatts_register_callback(esp_gatts_cb_t callback) { (void)callback; return ESP_OK; }
esp_err_t esp_ble_gatts_app_register(uint16_t app_id) { (void)app_id; return ESP_OK; }
esp_err_t esp_ble_gap_start_advertising(const esp_ble_adv_params_t *params) { (void)params; return ESP_OK; }

esp_err_t nvs_open(const char *name, nvs_open_mode_t open_mode, nvs_handle_t *out_handle)
{
    (void)name; (void)open_mode; if (out_handle) *out_handle = 1; return ESP_OK;
}

esp_err_t nvs_get_str(nvs_handle_t handle, const char *key, char *out_value, size_t *length)
{
    (void)handle; (void)key;
    if (out_value && length && *length > 0) { out_value[0] = '\0'; *length = 1; }
    return ESP_FAIL;
}

esp_err_t nvs_set_str(nvs_handle_t handle, const char *key, const char *value)
{
    (void)handle; (void)key; (void)value; return ESP_OK;
}

esp_err_t nvs_get_u8(nvs_handle_t handle, const char *key, uint8_t *out_value)
{
    (void)handle; (void)key; if (out_value) *out_value = 0; return ESP_FAIL;
}

esp_err_t nvs_set_u8(nvs_handle_t handle, const char *key, uint8_t value)
{
    (void)handle; (void)key; (void)value; return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle)
{
    (void)handle; return ESP_OK;
}

void nvs_close(nvs_handle_t handle)
{
    (void)handle;
}

