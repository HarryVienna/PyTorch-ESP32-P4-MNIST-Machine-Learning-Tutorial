#include "canvas_paint.h"
#include "../ui/screens.h"
#include "driver/ppa.h"
#include "esp_heap_caps.h"
#include "esp_private/esp_cache_private.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "CANVAS";

#define BRUSH_RADIUS     10

/* Canvas is 448x448 (28*16), scale by 1/16 → exactly 28x28 output */
#define PPA_SCALE_FACTOR (1.0f / 16.0f)

static lv_color_t *canvas_buf;
static lv_coord_t last_x = -1;
static lv_coord_t last_y = -1;
static bool canvas_empty = true;


static void draw_filled_circle(int cx, int cy, int r)
{
    int r2 = r * r;

    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy <= r2) {
                int px = cx + dx, py = cy + dy;
                if (px >= 0 && px < CANVAS_WIDTH && py >= 0 && py < CANVAS_HEIGHT)
                    ((uint16_t *)canvas_buf)[py * CANVAS_WIDTH + px] = 0xFFFF;
            }
        }
    }
}

static void draw_line(int x0, int y0, int x1, int y1)
{
    int dx = abs(x1 - x0);
    int dy = -abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    for (;;) {
        draw_filled_circle(x0, y0, BRUSH_RADIUS);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void canvas_clear(void)
{
    lv_color_t black = lv_color_black();
    for (int i = 0; i < CANVAS_WIDTH * CANVAS_HEIGHT; i++) {
        canvas_buf[i] = black;
    }
    lv_obj_invalidate(objects.draw_area);
}

static void on_canvas_press(lv_event_t *e)
{
    lv_obj_t *canvas = lv_event_get_target(e);
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;

    lv_point_t point;
    lv_indev_get_point(indev, &point);

    lv_coord_t x = point.x - canvas->coords.x1;
    lv_coord_t y = point.y - canvas->coords.y1;

    canvas_empty = false;

    if (last_x >= 0 && last_y >= 0) {
        draw_line(last_x, last_y, x, y);
    } else {
        draw_filled_circle(x, y, BRUSH_RADIUS);
    }

    last_x = x;
    last_y = y;

    lv_obj_invalidate(canvas);
}

static void on_canvas_release(lv_event_t *e)
{
    (void)e;
    last_x = -1;
    last_y = -1;
}

void action_button_reset(lv_event_t *e)
{
    (void)e;
    canvas_clear();
    canvas_empty = true;
    last_x = -1;
    last_y = -1;
}

bool canvas_paint_is_empty(void)
{
    return canvas_empty;
}

void canvas_paint_init(void)
{
    lv_obj_t *canvas = objects.draw_area;

    canvas_buf = heap_caps_malloc(
        CANVAS_WIDTH * CANVAS_HEIGHT * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    lv_canvas_set_buffer(canvas, canvas_buf, CANVAS_WIDTH, CANVAS_HEIGHT,
                         LV_IMG_CF_TRUE_COLOR);
    canvas_clear();

    lv_obj_add_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(canvas, on_canvas_press, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(canvas, on_canvas_release, LV_EVENT_RELEASED, NULL);
}

/* ── PPA hardware-accelerated resize (448x448 → 28x28) ──────────────── */

static ppa_client_handle_t ppa_srm_handle;
static lv_color_t *srm_out_buf;        /* 28x28 RGB565, cache-aligned */
static size_t srm_out_buf_size;
static uint8_t gray_buf[PPA_OUT_SIZE * PPA_OUT_SIZE];

void ppa_init(void)
{
    ppa_client_config_t client_cfg = {
        .oper_type = PPA_OPERATION_SRM,
        .max_pending_trans_num = 1,
    };
    ESP_ERROR_CHECK(ppa_register_client(&client_cfg, &ppa_srm_handle));

    /* Allocate cache-aligned output buffer in internal RAM (only 1568 bytes) */
    size_t alignment = 0;
    esp_cache_get_alignment(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA, &alignment);
    /* Must match L2 cache line size (128B with CONFIG_CACHE_L2_CACHE_LINE_128B) */
    if (alignment < CONFIG_CACHE_L2_CACHE_LINE_SIZE) alignment = CONFIG_CACHE_L2_CACHE_LINE_SIZE;

    size_t raw_size = PPA_OUT_SIZE * PPA_OUT_SIZE * sizeof(lv_color_t);
    srm_out_buf_size = (raw_size + alignment - 1) & ~(alignment - 1);

    srm_out_buf = heap_caps_aligned_calloc(alignment, 1, srm_out_buf_size,
                                           MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    ESP_LOGI(TAG, "PPA SRM initialised (align=%u, buf=%u bytes)",
             (unsigned)alignment, (unsigned)srm_out_buf_size);
}

const uint8_t *ppa_resize(void)
{
    ppa_srm_oper_config_t srm_cfg = {
        .in = {
            .buffer     = canvas_buf,
            .pic_w      = CANVAS_WIDTH,
            .pic_h      = CANVAS_HEIGHT,
            .block_w    = CANVAS_WIDTH,
            .block_h    = CANVAS_HEIGHT,
            .block_offset_x = 0,
            .block_offset_y = 0,
            .srm_cm     = PPA_SRM_COLOR_MODE_RGB565,
        },
        .out = {
            .buffer      = srm_out_buf,
            .buffer_size = srm_out_buf_size,
            .pic_w       = PPA_OUT_SIZE,
            .pic_h       = PPA_OUT_SIZE,
            .block_offset_x = 0,
            .block_offset_y = 0,
            .srm_cm      = PPA_SRM_COLOR_MODE_RGB565,
        },
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
        .scale_x        = PPA_SCALE_FACTOR,
        .scale_y        = PPA_SCALE_FACTOR,
        .mode           = PPA_TRANS_MODE_BLOCKING,
    };

    esp_err_t ret = ppa_do_scale_rotate_mirror(ppa_srm_handle, &srm_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PPA SRM failed: %s", esp_err_to_name(ret));
        memset(gray_buf, 0, sizeof(gray_buf));
        return gray_buf;
    }

    /* Convert RGB565 → grayscale uint8 */
    for (int i = 0; i < PPA_OUT_SIZE * PPA_OUT_SIZE; i++) {
        uint16_t px = ((uint16_t *)srm_out_buf)[i];
        uint8_t r = (px >> 11) & 0x1F;
        uint8_t g = (px >> 5)  & 0x3F;
        uint8_t b = px & 0x1F;
        /* Expand to 8-bit */
        r = (r << 3) | (r >> 2);
        g = (g << 2) | (g >> 4);
        b = (b << 3) | (b >> 2);
        /* Luminance: 0.299*R + 0.587*G + 0.114*B */
        gray_buf[i] = (uint8_t)((r * 77 + g * 150 + b * 29) >> 8);
    }

    return gray_buf;
}
