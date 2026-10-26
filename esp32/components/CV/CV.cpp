#include <stdio.h>
#include "CV.h"
#include "coco_detect.hpp"
#include "dl_image.hpp"
#include "esp_camera.h"

COCODetect *detect = NULL;

extern "C" void init_detect(void)
{
    detect = new COCODetect(COCODetect::YOLO11N_S8_V1);
}


uint32_t process_jpeg_image(dl::image::jpeg_img_t jpeg_img, uint32_t coco_class){

    auto img = dl::image::sw_decode_jpeg(jpeg_img, dl::image::DL_IMAGE_PIX_TYPE_RGB888);

    std::list<dl::detect::result_t> &res = detect->run(img);

    uint32_t cnt = 0;

    for(auto &result : res){
        printf("Category: %d, Score: %f, Box: [%d, %d, %d, %d]\n", result.category, result.score, result.box[0], result.box[1], result.box[2], result.box[3]);
        if(result.category == coco_class){
            cnt += 1;
        }
    }

    return cnt;
}

extern "C" uint32_t process_image_from_camera(camera_fb_t *fb, uint32_t coco_class){

    dl::image::jpeg_img_t jpeg_img = {.data=fb->buf, .data_len =fb->len};

    return process_jpeg_image(jpeg_img, coco_class);

}

extern "C" uint32_t process_image_from_mem(uint8_t *start, uint32_t len, uint32_t coco_class){

    dl::image::jpeg_img_t jpeg_img = {.data=start, .data_len = len};

    return process_jpeg_image(jpeg_img, coco_class);

}



extern "C" void deinit_detect(void)
{
    delete detect;
}

