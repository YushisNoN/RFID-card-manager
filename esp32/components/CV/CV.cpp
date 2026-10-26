#include <stdio.h>
#include "CV.h"
#include "coco_detect.hpp"
#include "dl_image.hpp"
#include "esp_camera.h"

COCODetect *detect = NULL;

extern "C" void init(void)
{
    detect = new COCODetect(COCODetect::YOLO11N_S8_V1);
}

extern "C" std::__cxx11::list<dl::detect::result_t> process_image(camera_fb_t *fb){

    dl::image::jpeg_img_t jpeg_img = {.data=fb->buf, .data_len =fb->len};

    auto img = dl::image::sw_decode_jpeg(jpeg_img, dl::image::DL_IMAGE_PIX_TYPE_RGB888);

    std::list<dl::detect::result_t> &res = detect->run(img);

    return res;
}

extern "C" void deinit(void)
{
    delete detect;
}

