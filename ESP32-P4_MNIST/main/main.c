/**
 * @file main.c
 * @brief LVGL "Hello World" for the Elecrow ESP32-P4 display
 *
 * Minimal example that initialises the 1024x600 TFT display with touch and
 * shows a "Hello World!" label plus a button to verify touch input.
 *
 * All display hardware setup (LDO, I2C, touch, MIPI DSI, backlight, LVGL)
 * is handled inside display_init() – see display.c for details.
 */

#include "esp_err.h"
#include "esp_log.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"

#include "display/display.h"
#include "ui/ui.h"
#include "gui/canvas_paint.h"
#include "gui/chart_display.h"
#include "mnist/mnist_task.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting...");

    /* Initialise the complete display subsystem in one call */
    ESP_ERROR_CHECK(display_init());

    lvgl_port_lock(0);
    ui_init();
    canvas_paint_init();
    chart_display_init();
    lvgl_port_unlock();

    ppa_init();
    mnist_task_start();

    ESP_LOGI(TAG, "Application started");
}
