#include <stdio.h>
#include "web-client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
void app_main(void)
{
    web_client_init();
    vTaskDelay(1000); // Надо будет заменить на EventGroup или Semaphore 
    check_stud_uid(10);
}
