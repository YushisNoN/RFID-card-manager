#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "rfid/RFID.h"

void app_main(void)
{

    
    printf("Begin...\n");
    fflush(stdout);
    RFIDinit();
    printf("Rfid init...\n");
    fflush(stdout);
    while(1) {
        fflush(stdout);
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
