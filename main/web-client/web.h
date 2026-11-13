#ifndef WEB_INCL
#define WEB_INCL

#include "RFID.h"

#include <stdio.h>
#include <string.h>

#include "esp_http_client.h"
#include "esp_log.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/event_groups.h"







esp_err_t sendRequest(RFIDCard uid);


void wifi_init_sta(void);
#endif