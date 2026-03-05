#ifndef CHART_DISPLAY_H
#define CHART_DISPLAY_H

#include <lvgl.h>
#include "../mnist/mnist_inference.h"

#ifdef __cplusplus
extern "C" {
#endif

void chart_display_init(void);
void chart_display_update(const float scores[MNIST_NUM_CLASSES]);

#ifdef __cplusplus
}
#endif

#endif /* CHART_DISPLAY_H */
