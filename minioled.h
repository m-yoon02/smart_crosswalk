// minioled.h - MiniOLED for the PIC24F16KA101 (only 1.5 KB of RAM)
// Instead of a 1 KB framebuffer, the screen is drawn one 8-row "page"
// at a time using a single 128-byte buffer.
#ifndef MINIOLED_H
#define MINIOLED_H

#include <stdint.h>
#include <stdbool.h>

bool oled_begin(uint8_t address);                     // usually 0x3C
void oled_show_text(const char* text, uint8_t size);  // clear + draw text from top-left

#endif
