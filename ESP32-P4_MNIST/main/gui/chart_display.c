#include "chart_display.h"
#include "../ui/screens.h"
#include "esp_lvgl_port.h"
#include <math.h>
#include <stdbool.h>

/* 0 = Softmax (true probabilities), 1 = Raw scores (min-max scaled) */
#define CHART_MODE_RAW 1

static lv_chart_series_t *series;

static void draw_event_cb(lv_event_t *e)
{
    lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
    if (dsc->part != LV_PART_TICKS || dsc->id != LV_CHART_AXIS_PRIMARY_X) return;
    if (dsc->value >= 0 && dsc->value <= 9) {
        lv_snprintf(dsc->text, dsc->text_length, "%ld", dsc->value);
    }
}

void chart_display_init(void)
{
    lv_obj_t *chart = objects.propability_chart;

    lv_chart_set_type(chart, LV_CHART_TYPE_BAR);
    lv_chart_set_point_count(chart, MNIST_NUM_CLASSES);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);

    /* X-axis tick labels: 10 major ticks for digits 0-9 */
    lv_chart_set_axis_tick(chart, LV_CHART_AXIS_PRIMARY_X,
                           0, 0, MNIST_NUM_CLASSES, 1, true, 20);
    /* Y-axis: percentage 0-100 */
    lv_chart_set_axis_tick(chart, LV_CHART_AXIS_PRIMARY_Y,
                           5, 3, 6, 2, true, 40);

    lv_obj_add_event_cb(chart, draw_event_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);

    series = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_BLUE),
                                 LV_CHART_AXIS_PRIMARY_Y);

    /* Initialize all bars to zero */
    for (int i = 0; i < MNIST_NUM_CLASSES; i++) {
        lv_chart_set_value_by_id(chart, series, i, 0);
    }

    lv_chart_refresh(chart);
}

void chart_display_update(const float scores[MNIST_NUM_CLASSES])
{
    if (!lvgl_port_lock(10)) return;

    lv_obj_t *chart = objects.propability_chart;

    /* All scores equal (e.g. empty canvas) → set all bars to 0 */
    bool all_equal = true;
    float max_score = scores[0];
    float min_score = scores[0];
    for (int i = 1; i < MNIST_NUM_CLASSES; i++) {
        if (scores[i] > max_score) max_score = scores[i];
        if (scores[i] < min_score) min_score = scores[i];
        if (scores[i] != scores[0]) all_equal = false;
    }
    if (all_equal) {
        for (int i = 0; i < MNIST_NUM_CLASSES; i++)
            lv_chart_set_value_by_id(chart, series, i, 0);
        lv_chart_refresh(chart);
        lvgl_port_unlock();
        return;
    }

#if CHART_MODE_RAW
    /* Raw scores: min-max scaled to 0-100 */
    // float range = max_score - min_score;
    // if (range < 1e-6f) range = 1.0f;
    // for (int i = 0; i < MNIST_NUM_CLASSES; i++) {
    //     int pct = (int)((scores[i] - min_score) / range * 100.0f + 0.5f);
    //     lv_chart_set_value_by_id(chart, series, i, pct);
    // }
    const float ABSOLUTE_MAX = 15.0f; 

    for (int i = 0; i < MNIST_NUM_CLASSES; i++) {
        /* Negative Werte kappen wir auf 0, da sie absolute Unsicherheit bedeuten */
        float val = scores[i] > 0.0f ? scores[i] : 0.0f;
        
        /* Prozentwert berechnen basierend auf dem festen Maximalwert */
        int pct = (int)((val / ABSOLUTE_MAX) * 100.0f + 0.5f);
        
        /* Sicherstellen, dass der Balken nicht über das Diagramm hinausschießt */
        if (pct > 100) pct = 100;
        
        lv_chart_set_value_by_id(chart, series, i, pct);
    }
#else
    /* Softmax: true probabilities (%) */
    float sum = 0.0f;
    float probs[MNIST_NUM_CLASSES];
    for (int i = 0; i < MNIST_NUM_CLASSES; i++) {
        probs[i] = expf(scores[i] - max_score);
        sum += probs[i];
    }
    for (int i = 0; i < MNIST_NUM_CLASSES; i++) {
        int pct = (int)(probs[i] / sum * 100.0f + 0.5f);
        lv_chart_set_value_by_id(chart, series, i, pct);
    }
#endif

    lv_chart_refresh(chart);
    lvgl_port_unlock();
}
