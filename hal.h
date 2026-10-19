// hal.h - everything that touches the hardware.
// Only hal_pic24.c knows about PIC24 registers; the rest is plain C.
#ifndef HAL_H
#define HAL_H

#include <stdint.h>
#include <stdbool.h>

void     hal_init(void);
uint32_t millis(void);                 // milliseconds since power-on (Timer1)

void lights_set(bool carRed, bool carYellow, bool carGreen,
                bool pedRed, bool pedGreen);
bool car_detected(void);               // true while the sensor sees a car

int  uart_read(void);                  // next byte from HC-05, or -1 if none
void uart_print(const char* s);        // send text to the phone

void i2c_start(void);
bool i2c_write(uint8_t b);             // returns true if the device ACKed
void i2c_stop(void);

#endif
