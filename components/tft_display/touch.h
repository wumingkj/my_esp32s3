#ifndef TOUCH_H
#define TOUCH_H

#include "esp_err.h"

esp_err_t touch_init(int cs_pin, int clk_pin, int din_pin, int dout_pin, int irq_pin);
esp_err_t touch_read(uint16_t* x, uint16_t* y, uint8_t* pressed);

#endif  // TOUCH_H