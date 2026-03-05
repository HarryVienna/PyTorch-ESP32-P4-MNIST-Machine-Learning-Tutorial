/**
 * @file display.c
 * @brief Complete display driver for Elecrow ESP32-P4 7" display module
 *
 * This file consolidates all hardware initialization that is needed to bring
 * up the 1024x600 TFT display with capacitive touch on the Elecrow ESP32-P4
 * board.  It replaces the former bsp_i2c, bsp_display, and bsp_illuminate
 * components with a single, self-contained source file.
 *
 * Initialization order (executed by display_init()):
 *   1. LDO regulators   – power the display and touch controller
 *   2. I2C master bus    – communication channel for the touch controller
 *   3. GT911 touch       – capacitive touch panel driver
 *   4. LCD backlight     – PWM-controlled via LEDC peripheral
 *   5. MIPI DSI + panel  – EK79007 LCD controller configuration
 *   6. LVGL              – graphics library with display and touch bindings
 *   7. Backlight on      – turn on backlight after everything is ready
 */

/* ─── Includes ────────────────────────────────────────────────────────────── */

#include "display.h"

#include <stdio.h>

/* ESP-IDF core */
#include "esp_err.h"
#include "esp_log.h"
#include "esp_ldo_regulator.h"

/* I2C driver (new master API) */
#include "driver/i2c_master.h"

/* GPIO + LEDC PWM for backlight */
#include "driver/gpio.h"
#include "driver/ledc.h"

/* LCD panel / MIPI DSI */
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_ek79007.h"

/* Touch controller */
#include "esp_lcd_touch_gt911.h"

/* LVGL + ESP LVGL port */
#include "lvgl.h"
#include "esp_lvgl_port.h"

/* FreeRTOS (needed for task priorities) */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ─── Logging tag ─────────────────────────────────────────────────────────── */

static const char *TAG = "DISPLAY";

/* ─── Display parameters ─────────────────────────────────────────────────── */

/** Horizontal resolution of the LCD panel in pixels. */
#define LCD_H_RES           1024

/** Vertical resolution of the LCD panel in pixels. */
#define LCD_V_RES           600

/** Colour depth – 16 bits per pixel (RGB565). */
#define LCD_BITS_PER_PIXEL  16

/* ─── Pin definitions ─────────────────────────────────────────────────────── */

/** I2C SDA pin – directly connected to the GT911 touch controller. */
#define I2C_GPIO_SDA        45

/** I2C SCL pin – directly connected to the GT911 touch controller. */
#define I2C_GPIO_SCL        46

/** GT911 hardware-reset pin (directly active-low). */
#define TOUCH_GPIO_RST      40

/** GT911 interrupt pin (directly active-low). */
#define TOUCH_GPIO_INT      42

/** LCD backlight control pin – driven by LEDC PWM. */
#define LCD_GPIO_BL         31

/** Backlight PWM frequency in Hz. */
#define BL_PWM_FREQ_HZ      30000

/* ─── Private handles (module-internal state) ─────────────────────────────── */

/** I2C master bus handle – used by the touch controller. */
static i2c_master_bus_handle_t   s_i2c_bus     = NULL;

/** GT911 touch panel handle – passed to LVGL as input device. */
static esp_lcd_touch_handle_t    s_touch       = NULL;

/** I2C panel-IO handle used by the GT911 driver. */
static esp_lcd_panel_io_handle_t s_touch_io    = NULL;

/** MIPI DSI bus handle. */
static esp_lcd_dsi_bus_handle_t  s_dsi_bus     = NULL;

/** MIPI DBI (command) IO handle – used by the EK79007 panel driver. */
static esp_lcd_panel_io_handle_t s_dbi_io      = NULL;

/** LCD panel handle (EK79007). */
static esp_lcd_panel_handle_t    s_panel       = NULL;

/** LVGL display object. */
static lv_display_t             *s_lvgl_disp   = NULL;

/** LVGL touch input device. */
static lv_indev_t               *s_lvgl_touch  = NULL;

/** LDO channel handles – kept alive for the lifetime of the application. */
static esp_ldo_channel_handle_t  s_ldo3        = NULL;
static esp_ldo_channel_handle_t  s_ldo4        = NULL;

/* ═════════════════════════════════════════════════════════════════════════════
 *  1. LDO Power Regulators
 *
 *  The ESP32-P4 has on-chip LDO regulators.  The Elecrow display board uses:
 *    - LDO3 at 2.5 V  (MIPI DSI PHY power)
 *    - LDO4 at 3.3 V  (general peripheral power)
 *
 *  These must be enabled before any display or touch hardware is accessed.
 * ═══════════════════════════════════════════════════════════════════════════ */

static esp_err_t ldo_init(void)
{
    esp_err_t ret;

    /* LDO3 – 2.5 V for the MIPI DSI PHY */
    const esp_ldo_channel_config_t ldo3_cfg = {
        .chan_id    = 3,
        .voltage_mv = 2500,
    };
    ret = esp_ldo_acquire_channel(&ldo3_cfg, &s_ldo3);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LDO3 (2.5 V) init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* LDO4 – 3.3 V for peripherals */
    const esp_ldo_channel_config_t ldo4_cfg = {
        .chan_id    = 4,
        .voltage_mv = 3300,
    };
    ret = esp_ldo_acquire_channel(&ldo4_cfg, &s_ldo4);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LDO4 (3.3 V) init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "LDO3 (2.5 V) and LDO4 (3.3 V) enabled");
    return ESP_OK;
}

/* ═════════════════════════════════════════════════════════════════════════════
 *  2. I2C Master Bus
 *
 *  A single I2C master bus at 400 kHz is used to communicate with the GT911
 *  touch controller.  The bus handle is stored in s_i2c_bus and later passed
 *  to the touch driver.
 *
 *  Wiring:
 *    SDA ── GPIO 45
 *    SCL ── GPIO 46
 * ═══════════════════════════════════════════════════════════════════════════ */

static esp_err_t i2c_init(void)
{
    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port               = 0,                    /* I2C port 0                   */
        .sda_io_num             = I2C_GPIO_SDA,         /* SDA on GPIO 45               */
        .scl_io_num             = I2C_GPIO_SCL,         /* SCL on GPIO 46               */
        .clk_source             = I2C_CLK_SRC_DEFAULT,  /* default clock source         */
        .glitch_ignore_cnt      = 7,                    /* filter glitches < 7 cycles   */
        .flags.enable_internal_pullup = true,           /* enable internal pull-ups     */
    };

    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &s_i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C master bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "I2C master bus initialised (SDA=%d, SCL=%d, 400 kHz)",
             I2C_GPIO_SDA, I2C_GPIO_SCL);
    return ESP_OK;
}

/* ═════════════════════════════════════════════════════════════════════════════
 *  3. GT911 Capacitive Touch Controller
 *
 *  The GT911 sits on the I2C bus.  Its primary address is 0x5D; some modules
 *  use the backup address 0x14 – we try both if needed.
 *
 *  Configuration:
 *    - Resolution matches the LCD: 1024 x 600
 *    - RST pin = GPIO 40, INT pin = GPIO 42
 *    - No coordinate swap or mirror (landscape orientation)
 * ═══════════════════════════════════════════════════════════════════════════ */

static esp_err_t touch_init(void)
{
    esp_err_t ret;

    /* Panel-IO config: I2C settings for the GT911 */
    esp_lcd_panel_io_i2c_config_t io_cfg = {
        .dev_addr              = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS,  /* 0x5D */
        .control_phase_bytes   = 1,
        .dc_bit_offset         = 0,
        .lcd_cmd_bits          = 16,          /* GT911 uses 16-bit register addresses */
        .flags = {
            .disable_control_phase = 1,
        },
        .scl_speed_hz          = 400000,      /* 400 kHz I2C clock */
    };

    /* Touch panel config */
    const esp_lcd_touch_config_t tp_cfg = {
        .x_max          = LCD_H_RES,          /* max X = 1024 */
        .y_max          = LCD_V_RES,          /* max Y = 600  */
        .rst_gpio_num   = TOUCH_GPIO_RST,     /* GPIO 40 */
        .int_gpio_num   = TOUCH_GPIO_INT,     /* GPIO 42 */
        .levels = {
            .reset     = 0,                   /* active-low reset  */
            .interrupt = 0,                   /* active-low IRQ    */
        },
        .flags = {
            .swap_xy  = false,
            .mirror_x = false,
            .mirror_y = false,
        },
    };

    /* Create the I2C panel-IO and attach the GT911 driver */
    ret = esp_lcd_new_panel_io_i2c(s_i2c_bus, &io_cfg, &s_touch_io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Touch panel IO (I2C) creation failed: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    ret = esp_lcd_touch_new_i2c_gt911(s_touch_io, &tp_cfg, &s_touch);
    if (ret != ESP_OK) {
        /*
         * Some GT911 modules respond on the backup address (0x14) instead of
         * the primary one (0x5D).  Retry with the backup address.
         */
        ESP_LOGW(TAG, "GT911 not found at primary address – trying backup");
        io_cfg.dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP;  /* 0x14 */
        ret = esp_lcd_new_panel_io_i2c(s_i2c_bus, &io_cfg, &s_touch_io);
        if (ret != ESP_OK) return ret;
        ret = esp_lcd_touch_new_i2c_gt911(s_touch_io, &tp_cfg, &s_touch);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "GT911 init failed on both addresses");
            return ret;
        }
    }

    ESP_LOGI(TAG, "GT911 touch controller initialised");
    return ESP_OK;
}

/* ═════════════════════════════════════════════════════════════════════════════
 *  4. LCD Backlight (LEDC PWM)
 *
 *  The backlight is driven by a single GPIO via the LEDC (LED Control)
 *  peripheral configured for PWM output.
 *
 *  Configuration:
 *    - GPIO 31, LEDC channel 0, timer 0
 *    - 30 kHz PWM, 11-bit duty resolution (0..2047)
 *    - Brightness is mapped from 0..100 % to a duty-cycle value.
 * ═══════════════════════════════════════════════════════════════════════════ */

static esp_err_t backlight_init(void)
{
    esp_err_t ret;

    /* Configure the GPIO as output first */
    const gpio_config_t bl_gpio_cfg = {
        .pin_bit_mask  = (1ULL << LCD_GPIO_BL),
        .mode          = GPIO_MODE_OUTPUT,
        .pull_up_en    = false,
        .pull_down_en  = false,
        .intr_type     = GPIO_INTR_DISABLE,
    };
    ret = gpio_config(&bl_gpio_cfg);
    if (ret != ESP_OK) return ret;

    /* LEDC timer – determines the PWM frequency and resolution */
    const ledc_timer_config_t timer_cfg = {
        .clk_cfg         = LEDC_USE_PLL_DIV_CLK,
        .duty_resolution = LEDC_TIMER_11_BIT,   /* 0..2047 */
        .freq_hz         = BL_PWM_FREQ_HZ,      /* 30 kHz  */
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = LEDC_TIMER_0,
    };
    ret = ledc_timer_config(&timer_cfg);
    if (ret != ESP_OK) return ret;

    /* LEDC channel – binds the timer to the GPIO pin */
    const ledc_channel_config_t ch_cfg = {
        .gpio_num   = LCD_GPIO_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_0,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = LEDC_TIMER_0,
        .duty       = 0,                /* start with backlight off */
        .hpoint     = 0,
    };
    ret = ledc_channel_config(&ch_cfg);
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "Backlight PWM initialised on GPIO %d (%d Hz)",
             LCD_GPIO_BL, BL_PWM_FREQ_HZ);
    return ESP_OK;
}

/**
 * @brief Set the LCD backlight brightness.
 *
 * @param brightness  Brightness percentage (0 = off, 100 = maximum).
 *
 * The duty cycle is calculated as:  duty = brightness * 18 + 200
 * This formula accounts for the minimum forward voltage of the backlight
 * LED so that brightness=1 already produces visible light.
 */
esp_err_t display_set_backlight(uint32_t brightness)
{
    uint32_t duty = (brightness > 0) ? (brightness * 18 + 200) : 0;
    esp_err_t ret = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    if (ret != ESP_OK) return ret;
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

/* ═════════════════════════════════════════════════════════════════════════════
 *  5. MIPI DSI Bus + EK79007 LCD Panel
 *
 *  The EK79007 is a MIPI DSI display controller with the following setup:
 *    - 2 data lanes at 900 Mbps each
 *    - DPI (video-mode) interface at 51 MHz pixel clock
 *    - 1024x600 resolution, RGB565 colour format
 *    - DMA2D acceleration enabled for fast frame-buffer transfers
 *
 *  Video timing values (back-porch, front-porch, sync-pulse) are taken from
 *  the EK79007 datasheet and match the Elecrow display module.
 * ═══════════════════════════════════════════════════════════════════════════ */

static esp_err_t lcd_panel_init(void)
{
    esp_err_t ret;

    /* ── MIPI DSI bus ── */
    const esp_lcd_dsi_bus_config_t dsi_bus_cfg = {
        .bus_id             = 0,
        .num_data_lanes     = 2,                           /* 2 data lanes  */
        .phy_clk_src        = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = 900,                         /* 900 Mbps/lane */
    };
    ret = esp_lcd_new_dsi_bus(&dsi_bus_cfg, &s_dsi_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "MIPI DSI bus creation failed");
        return ret;
    }

    /* ── DBI (command) interface ── */
    const esp_lcd_dbi_io_config_t dbi_cfg = {
        .virtual_channel = 0,
        .lcd_cmd_bits    = 8,
        .lcd_param_bits  = 8,
    };
    ret = esp_lcd_new_panel_io_dbi(s_dsi_bus, &dbi_cfg, &s_dbi_io);
    if (ret != ESP_OK) return ret;

    /* ── Determine pixel format from BITS_PER_PIXEL ── */
    lcd_color_rgb_pixel_format_t px_fmt;
    if (LCD_BITS_PER_PIXEL == 24)
        px_fmt = LCD_COLOR_PIXEL_FORMAT_RGB888;
    else if (LCD_BITS_PER_PIXEL == 18)
        px_fmt = LCD_COLOR_PIXEL_FORMAT_RGB666;
    else
        px_fmt = LCD_COLOR_PIXEL_FORMAT_RGB565;  /* default: 16 bpp */

    /* ── DPI (video-mode) panel configuration ── */
    const esp_lcd_dpi_panel_config_t dpi_cfg = {
        .dpi_clk_src        = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = 51,                 /* 51 MHz pixel clock */
        .virtual_channel    = 0,
        .pixel_format       = px_fmt,
        .num_fbs            = 2,                   /* double buffer: DPI reads one, LVGL writes other */
        .video_timing = {
            .h_size             = LCD_H_RES,       /* 1024 px */
            .v_size             = LCD_V_RES,       /* 600 px  */
            .hsync_back_porch   = 160,
            .hsync_pulse_width  = 70,
            .hsync_front_porch  = 160,
            .vsync_back_porch   = 23,
            .vsync_pulse_width  = 10,
            .vsync_front_porch  = 12,
        },
        .flags.use_dma2d = true,                   /* use DMA2D for transfers */
    };

    /* ── EK79007 vendor-specific configuration ── */
    ek79007_vendor_config_t vendor_cfg = {
        .mipi_config = {
            .dsi_bus    = s_dsi_bus,
            .dpi_config = &dpi_cfg,
        },
    };

    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num  = -1,                     /* no dedicated reset pin */
        .rgb_ele_order   = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel  = LCD_BITS_PER_PIXEL,
        .vendor_config   = &vendor_cfg,
    };

    /* Create, reset and initialise the panel */
    ret = esp_lcd_new_panel_ek79007(s_dbi_io, &panel_cfg, &s_panel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "EK79007 panel creation failed");
        return ret;
    }
    ret = esp_lcd_panel_reset(s_panel);
    if (ret != ESP_OK) return ret;
    ret = esp_lcd_panel_init(s_panel);
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "LCD panel initialised (%dx%d, %d bpp, MIPI DSI)",
             LCD_H_RES, LCD_V_RES, LCD_BITS_PER_PIXEL);
    return ESP_OK;
}

/* ═════════════════════════════════════════════════════════════════════════════
 *  6. LVGL Initialisation
 *
 *  The ESP LVGL port library (esp_lvgl_port) creates a dedicated FreeRTOS
 *  task that drives the LVGL tick and rendering loop.  We register our LCD
 *  panel as the display and the GT911 as the touch input device.
 *
 *  Key settings:
 *    - LVGL task priority:  configMAX_PRIORITIES - 4
 *    - LVGL task stack:     16 KB
 *    - Timer period:        5 ms
 *    - Double-buffered frame buffer in SPIRAM
 *    - Touch input registered so LVGL receives touch events directly
 * ═══════════════════════════════════════════════════════════════════════════ */

static esp_err_t lvgl_init(void)
{
    esp_err_t ret;

    /* ── LVGL port (task) configuration ── */
    const lvgl_port_cfg_t port_cfg = {
        .task_priority    = configMAX_PRIORITIES - 4,
        .task_stack       = 8192 * 2,          /* 16 KB stack */
        .task_affinity    = -1,                /* no core affinity */
        .task_max_sleep_ms = 10,
        .timer_period_ms  = 5,
    };
    ret = lvgl_port_init(&port_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LVGL port init failed");
        return ret;
    }

    /* ── Display registration ── */
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle      = s_dbi_io,
        .panel_handle   = s_panel,
        .control_handle = s_panel,
        .buffer_size    = LCD_H_RES * LCD_V_RES * (LCD_BITS_PER_PIXEL / 8),
        .double_buffer  = true,
        .hres           = LCD_H_RES,
        .vres           = LCD_V_RES,
        .monochrome     = false,
#if LVGL_VERSION_MAJOR >= 9
        .color_format   = LV_COLOR_FORMAT_RGB565,
#endif
        .rotation = {
            .swap_xy  = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma    = false,
            .buff_spiram = true,               /* frame buffer in SPIRAM */
            .sw_rotate   = true,
#if LVGL_VERSION_MAJOR >= 9
            .swap_bytes  = true,
#endif
#if CONFIG_DISPLAY_LVGL_FULL_REFRESH
            .full_refresh = true,
#else
            .full_refresh = false,
#endif
#if CONFIG_DISPLAY_LVGL_DIRECT_MODE
            .direct_mode = true,
#else
            .direct_mode = false,
#endif
        },
    };

    const lvgl_port_display_dsi_cfg_t dsi_cfg = {
        .flags = {
#if CONFIG_DISPLAY_LVGL_AVOID_TEAR
            .avoid_tearing = true,
#else
            .avoid_tearing = false,
#endif
        },
    };

    s_lvgl_disp = lvgl_port_add_disp_dsi(&disp_cfg, &dsi_cfg);
    if (s_lvgl_disp == NULL) {
        ESP_LOGE(TAG, "LVGL display registration failed");
        return ESP_FAIL;
    }

    /* ── Touch input device registration ── */
    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp   = s_lvgl_disp,
        .handle = s_touch,
    };
    s_lvgl_touch = lvgl_port_add_touch(&touch_cfg);
    if (s_lvgl_touch == NULL) {
        ESP_LOGE(TAG, "LVGL touch input registration failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "LVGL initialised (display + touch)");
    return ESP_OK;
}

/* ═════════════════════════════════════════════════════════════════════════════
 *  Public API
 * ═══════════════════════════════════════════════════════════════════════════ */

esp_err_t display_init(void)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "Starting display subsystem initialisation...");

    /* Step 1 – Power regulators */
    ret = ldo_init();
    if (ret != ESP_OK) return ret;

    /* Step 2 – I2C bus (needed by the touch controller) */
    ret = i2c_init();
    if (ret != ESP_OK) return ret;

    /* Step 3 – GT911 touch controller */
    ret = touch_init();
    if (ret != ESP_OK) return ret;

    /* Step 4 – LCD backlight hardware (starts off) */
    ret = backlight_init();
    if (ret != ESP_OK) return ret;

    /* Step 5 – LCD panel (MIPI DSI + EK79007) */
    ret = lcd_panel_init();
    if (ret != ESP_OK) return ret;

    /* Step 6 – LVGL (display + touch registration) */
    ret = lvgl_init();
    if (ret != ESP_OK) return ret;

    /* Step 7 – Turn on backlight at full brightness */
    ret = display_set_backlight(100);
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "Display subsystem ready (1024x600, touch enabled)");
    return ESP_OK;
}
