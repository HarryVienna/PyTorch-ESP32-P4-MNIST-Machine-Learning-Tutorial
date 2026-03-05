#include "mnist_inference.h"
#include <cstring>
#include <cmath>
#include "esp_log.h"
#include "esp_spiffs.h"
#include "dl_model_base.hpp"

static const char *TAG = "MNIST_INF";

/* MNIST normalization constants (same as training) */
static const float MNIST_MEAN = 0.1307f;
static const float MNIST_STD  = 0.3081f;

static dl::Model *model = nullptr;

bool mnist_inference_init(void)
{
    /* Mount SPIFFS with model file */
    esp_vfs_spiffs_conf_t spiffs_conf = {
        .base_path = "/model",
        .partition_label = "model",
        .max_files = 2,
        .format_if_mount_failed = false,
    };
    esp_err_t ret = esp_vfs_spiffs_register(&spiffs_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(ret));
        return false;
    }

    /* Load model */
    ESP_LOGI(TAG, "Loading model...");
    model = new dl::Model("/model/mnist_advanced_cnn.espdl",
                          fbs::MODEL_LOCATION_IN_SDCARD);
    ESP_LOGI(TAG, "Model loaded.");

    /* Log input tensor info */
    dl::TensorBase *input = model->get_input();
    ESP_LOGI(TAG, "Input: %d bytes, dtype: %s, exponent: %d",
             input->get_bytes(), input->get_dtype_string(), input->get_exponent());

    return true;
}

bool mnist_inference_run(const uint8_t *gray_28x28, float scores[MNIST_NUM_CLASSES])
{
    if (!model) return false;

    dl::TensorBase *input = model->get_input();
    dl::dtype_t input_dtype = input->get_dtype();
    int input_exponent = input->get_exponent();

    /* Int8 model: normalize then quantize with model's exponent */
    float scale = powf(2.0f, (float)(-input_exponent));
    int8_t *dst = (int8_t *)input->get_element_ptr();
    for (int i = 0; i < 28 * 28; i++) {
        float pixel = gray_28x28[i] / 255.0f;
        float normalized = (pixel - MNIST_MEAN) / MNIST_STD;
        float quantized = roundf(normalized * scale);
        if (quantized > 127.0f) quantized = 127.0f;
        if (quantized < -128.0f) quantized = -128.0f;
        dst[i] = (int8_t)quantized;
    }

    /* Run inference */
    model->run();

    /* Read output scores */
    dl::TensorBase *output = model->get_output();
    int output_exponent = output->get_exponent();
    dl::dtype_t output_dtype = output->get_dtype();
    int num = output->get_size();
    if (num > MNIST_NUM_CLASSES) num = MNIST_NUM_CLASSES;

    for (int i = 0; i < num; i++) {
        int8_t raw = output->get_element<int8_t>(i);
        scores[i] = (float)raw * powf(2.0f, (float)output_exponent);
    }

    return true;
}

