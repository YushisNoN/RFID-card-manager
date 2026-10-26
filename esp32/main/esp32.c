#include <stdio.h>
#include "web-client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "CV.h"

extern const uint8_t test_jpeg_start[] asm("_binary_test3_jpg_start");
extern const uint8_t test_jpeg_end[]   asm("_binary_test3_jpg_end");

void app_main(void)
{
    web_client_init();
    vTaskDelay(1000); // Надо будет заменить на EventGroup или Semaphore 
    check_stud_uid(10);
    init_detect();
    uint32_t cnt = process_image_from_mem((uint8_t *)test_jpeg_start, test_jpeg_end - test_jpeg_start, 1);

}
