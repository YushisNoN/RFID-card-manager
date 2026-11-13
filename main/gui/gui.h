#ifndef GUI_H
#define GUI_H

#include "esp_lcd_panel_ops.h"
#include "esp_err.h"

#include <stdint.h>

typedef enum {
    GUI_STATE_WAITING,
    GUI_STATE_READING,
    GUI_STATE_SUCCESS,
    GUI_STATE_ERROR
} GUI_State;

esp_lcd_panel_handle_t gui_init(void);

void gui_show_waiting(void);
void gui_show_reading(void);
void gui_show_success(void);
void gui_show_error(void);

#endif