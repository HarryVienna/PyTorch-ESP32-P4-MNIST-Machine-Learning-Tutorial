#ifndef CANVAS_PAINT_H
#define CANVAS_PAINT_H

#include <lvgl.h>
#include <stdbool.h>
#include <stdint.h>

#define CANVAS_WIDTH  448
#define CANVAS_HEIGHT 448
#define PPA_OUT_SIZE  28

#ifdef __cplusplus
extern "C" {
#endif

void canvas_paint_init(void);
bool canvas_paint_is_empty(void);
void ppa_init(void);
const uint8_t *ppa_resize(void);

#ifdef __cplusplus
}
#endif

#endif /* CANVAS_PAINT_H */
