#include "touch.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "lvgl.h"
#include "power.h"

#define TAG "touch"

static bool has_touch = false;
#define TOUCH_INT_PIN 4
#define TOUCH_ADDR 0x38

static touch_calibration_t s_cal = {0, 0, 320, 240};

static lv_indev_t *s_indev;

static void IRAM_ATTR touch_isr(void *arg)
{
    power_register_activity();
}

static void lv_touch_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    uint16_t x, y;
    if (touch_read(&x, &y)) {
        data->state = LV_INDEV_STATE_PR;
        data->point.x = x;
        data->point.y = y;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

esp_err_t touch_init(void)
{
    nvs_flash_init();

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = 3,
        .scl_io_num = 2,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };
    esp_err_t err = i2c_param_config(I2C_NUM_0, &conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_param_config failed: %s", esp_err_to_name(err));
        return err;
    }
    err = i2c_driver_install(I2C_NUM_0, conf.mode, 0, 0, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "i2c_driver_install failed: %s", esp_err_to_name(err));
        return err;
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << TOUCH_INT_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed: %s", esp_err_to_name(err));
        return err;
    }
    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "isr service failed: %s", esp_err_to_name(err));
        return err;
    }
    err = gpio_isr_handler_add(TOUCH_INT_PIN, touch_isr, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "isr add failed: %s", esp_err_to_name(err));
        return err;
    }

    touch_calibration_t cal;
    if (touch_calibration_load(&cal) == ESP_OK) {
        s_cal = cal;
    }

    lv_indev_drv_t drv;
    lv_indev_drv_init(&drv);
    drv.type = LV_INDEV_TYPE_POINTER;
    drv.read_cb = lv_touch_read;
    s_indev = lv_indev_drv_register(&drv);

    has_touch = true;
    ESP_LOGI(TAG, "Touch controller initialized");
    return ESP_OK;
}

bool touch_read(uint16_t *x, uint16_t *y)
{
    if (!has_touch) return false;
    uint8_t reg = 0x02;
    uint8_t data[5];
    esp_err_t err = i2c_master_write_read_device(I2C_NUM_0, TOUCH_ADDR,
                                                 &reg, 1, data, sizeof(data),
                                                 pdMS_TO_TICKS(50));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c read failed: %s", esp_err_to_name(err));
        return false;
    }
    if ((data[0] & 0x0F) == 0) {
        return false;
    }
    uint16_t raw_x = ((data[1] & 0x0F) << 8) | data[2];
    uint16_t raw_y = ((data[3] & 0x0F) << 8) | data[4];

    int32_t cal_x = (int32_t)(raw_x - s_cal.x0) * 320 / (s_cal.x1 - s_cal.x0);
    int32_t cal_y = (int32_t)(raw_y - s_cal.y0) * 240 / (s_cal.y1 - s_cal.y0);

    if (cal_x < 0) cal_x = 0;
    if (cal_x > 319) cal_x = 319;
    if (cal_y < 0) cal_y = 0;
    if (cal_y > 239) cal_y = 239;

    *x = cal_x;
    *y = cal_y;
    return true;
}

esp_err_t touch_calibration_load(touch_calibration_t *cal)
{
    if (!cal) return ESP_ERR_INVALID_ARG;
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("touch", NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    size_t len = sizeof(touch_calibration_t);
    err = nvs_get_blob(nvs, "cal", cal, &len);
    nvs_close(nvs);
    return err;
}

esp_err_t touch_calibration_save(const touch_calibration_t *cal)
{
    if (!cal) return ESP_ERR_INVALID_ARG;
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("touch", NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(nvs, "cal", cal, sizeof(touch_calibration_t));
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

