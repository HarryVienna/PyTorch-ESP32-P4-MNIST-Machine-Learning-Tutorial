#ifndef MNIST_INFERENCE_H
#define MNIST_INFERENCE_H

#include <stdint.h>
#include <stdbool.h>

#define MNIST_NUM_CLASSES 10

#ifdef __cplusplus
extern "C" {
#endif

bool mnist_inference_init(void);
bool mnist_inference_run(const uint8_t *gray_28x28, float scores[MNIST_NUM_CLASSES]);

#ifdef __cplusplus
}
#endif

#endif /* MNIST_INFERENCE_H */
