#include "display.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "lvgl.h"

#define TAG "display"

static spi_device_handle_t st7789_handle;
static lv_disp_drv_t disp_drv;
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[320 * 10]; /* 10 lines buffer */

static void st7789_send_cmd(uint8_t cmd)
{
    spi_transaction_t t = {
        .flags = SPI_TRANS_USE_TXDATA,
        .length = 8,
        .tx_data = {cmd}
    };
    spi_device_polling_transmit(st7789_handle, &t);
}

static void st7789_send_data(const void *data, size_t len)
{
    if (!len) return;
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data
    };
    spi_device_polling_transmit(st7789_handle, &t);
}

static void disp_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    /* In a real driver the address window would be set here */
    size_t len = (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1) * sizeof(lv_color_t);
    st7789_send_data(color_p, len);
    lv_disp_flush_ready(drv);
}

void display_init(void)
{
    lv_init();

    spi_bus_config_t buscfg = {
        .mosi_io_num = 7,
        .miso_io_num = -1,
        .sclk_io_num = 6,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 40 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = 10,
        .queue_size = 7,
    };
    spi_bus_add_device(SPI2_HOST, &devcfg, &st7789_handle);

    ESP_LOGI(TAG, "ST7789 initialized");

    lv_disp_draw_buf_init(&draw_buf, buf, NULL, sizeof(buf) / sizeof(lv_color_t));
    lv_disp_drv_init(&disp_drv);
    disp_drv.flush_cb = disp_flush;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.hor_res = 240;
    disp_drv.ver_res = 320;
    lv_disp_drv_register(&disp_drv);
}

void display_update(void)
{
    lv_timer_handler();
}

