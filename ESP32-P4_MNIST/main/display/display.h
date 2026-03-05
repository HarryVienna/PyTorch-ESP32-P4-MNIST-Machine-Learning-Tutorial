/**
 * @file display.h
 * @brief Display driver for Elecrow ESP32-P4 7" display module
 *
 * This header provides the public API for initializing the complete display
 * subsystem of the Elecrow ESP32-P4 board. A single call to display_init()
 * sets up all required hardware:
 *
 *   - LDO power regulators (LDO3 = 2.5 V, LDO4 = 3.3 V)
 *   - I2C master bus (GPIO 45/46, 400 kHz) for the touch controller
 *   - GT911 capacitive touch controller via I2C
 *   - MIPI DSI bus and EK79007 LCD panel (1024x600, RGB565)
 *   - PWM backlight on GPIO 31
 *   - LVGL graphics library (display + touch input device)
 *
 * After display_init() returns successfully the LVGL task is running and
 * you can immediately create UI elements on lv_scr_act().
 *
 * Hardware overview (active pins):
 *
 *   Function          GPIO    Protocol / Notes
 *   ──────────────────────────────────────────────────
 *   I2C SDA           45      I2C master, 400 kHz
 *   I2C SCL           46      I2C master, 400 kHz
 *   Touch Reset       40      GT911 active-low reset
 *   Touch Interrupt   42      GT911 active-low IRQ
 *   LCD Backlight     31      LEDC PWM, 30 kHz
 *   MIPI DSI          —       2 data lanes, 900 Mbps
 *
 * Usage example:
 * @code
 *   #include "display.h"
 *
 *   void app_main(void) {
 *       ESP_ERROR_CHECK(display_init());
 *
 *       // LVGL is ready – create your UI here
 *       lvgl_port_lock(0);
 *       lv_obj_t *label = lv_label_create(lv_scr_act());
 *       lv_label_set_text(label, "Hello World!");
 *       lvgl_port_unlock();
 *   }
 * @endcode
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include "esp_err.h"

/**
 * @brief Initialize the complete display subsystem
 *
 * This function performs the full hardware bring-up in the correct order:
 *   1. Enable LDO3 (2.5 V) and LDO4 (3.3 V) power rails
 *   2. Initialize I2C master bus on GPIO 45 (SDA) / GPIO 46 (SCL)
 *   3. Initialize GT911 capacitive touch controller over I2C
 *   4. Initialize LCD backlight PWM on GPIO 31
 *   5. Configure MIPI DSI bus and EK79007 LCD panel (1024x600)
 *   6. Start LVGL and register display + touch input device
 *   7. Turn on backlight at full brightness
 *
 * After this call LVGL runs in its own FreeRTOS task.  All subsequent LVGL
 * API calls must be wrapped in lvgl_port_lock() / lvgl_port_unlock().
 *
 * @return ESP_OK on success, or an error code if any step fails.
 */
esp_err_t display_init(void);

/**
 * @brief Set the LCD backlight brightness
 *
 * @param brightness  Brightness percentage (0 = off, 100 = maximum).
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t display_set_backlight(uint32_t brightness);

#endif /* DISPLAY_H */
