#include "mnist_task.h"
#include "mnist_inference.h"
#include "../gui/canvas_paint.h"
#include "../gui/chart_display.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MNIST_TASK";

#define INFERENCE_INTERVAL_MS 10

static void inference_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "Initialising MNIST model...");
    if (!mnist_inference_init()) {
        ESP_LOGE(TAG, "Model init failed, inference task stopping.");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Inference task running (every %d ms)", INFERENCE_INTERVAL_MS);

    for (;;) {
        if (canvas_paint_is_empty()) {
            static const float zeros[MNIST_NUM_CLASSES] = {0};
            chart_display_update(zeros);
            vTaskDelay(pdMS_TO_TICKS(INFERENCE_INTERVAL_MS));
            continue;
        }

        const uint8_t *img = ppa_resize();

        float scores[MNIST_NUM_CLASSES];
        if (mnist_inference_run(img, scores)) {
            chart_display_update(scores);
        }

        vTaskDelay(pdMS_TO_TICKS(INFERENCE_INTERVAL_MS));
    }
}

void mnist_task_start(void)
{
    xTaskCreatePinnedToCore(inference_task, "inference", 32768, NULL, 4, NULL, 1);
}
