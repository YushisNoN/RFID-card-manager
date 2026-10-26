#ifndef CV_H
#define CV_H

#include <stdint.h>
#include "esp_camera.h"

#ifdef __cplusplus
extern "C" {
#endif

void     init_detect(void);
void     deinit_detect(void);
uint32_t process_image_from_camera(camera_fb_t *fb, uint32_t coco_class);
uint32_t process_image_from_mem(uint8_t *start, uint32_t len, uint32_t coco_class);

#ifdef __cplusplus
}
#endif

#endif