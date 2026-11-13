#ifndef RFID_INCL
#define RFID_INCL

#include<stdint.h>

typedef struct {
    uint8_t size;
    uint8_t bytes[10];
} RFIDCard;

void RFIDinit();

#endif