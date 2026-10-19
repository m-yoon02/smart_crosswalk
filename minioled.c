#include "minioled.h"
#include "hal.h"
#include "font5x7.h"
#include <string.h>

#define OLED_W 128
#define OLED_H 64
#define PAGES  (OLED_H / 8)

static uint8_t address;
static uint8_t line[OLED_W];   // one page: 128 columns x 8 rows

static void command(uint8_t c) {
  i2c_start();
  i2c_write(address << 1);   // 7-bit address + write bit
  i2c_write(0x00);           // control byte: a command follows
  i2c_write(c);
  i2c_stop();
}

bool oled_begin(uint8_t addr) {
  static const uint8_t init[] = {
    0xAE,        // display off
    0xD5, 0x80,  // clock
    0xA8, 0x3F,  // 64 rows
    0xD3, 0x00,  // no offset
    0x40,        // start line 0
    0x8D, 0x14,  // charge pump on
    0x20, 0x00,  // horizontal addressing
    0xA1, 0xC8,  // flip so (0,0) is top-left
    0xDA, 0x12,  // COM pins for 128x64
    0x81, 0xCF,  // contrast
    0xD9, 0xF1,  // pre-charge
    0xDB, 0x40,  // VCOMH
    0xA4, 0xA6,  // show RAM, not inverted
    0x2E,        // no scrolling
    0xAF         // display on
  };
  uint8_t i;
  bool found;

  address = addr;

  i2c_start();
  found = i2c_write(address << 1);   // does anything ACK at this address?
  i2c_stop();
  if (!found) return false;

  for (i = 0; i < sizeof(init); i++) command(init[i]);
  oled_show_text("", 1);
  return true;
}

// Draw every part of the text that falls inside one 8-row page into line[]
static void render_page(const char* text, uint8_t size, uint8_t page) {
  const int16_t charW = (FONT_WIDTH + 1) * size;
  const int16_t charH = (FONT_HEIGHT + 1) * size;
  const int16_t pageTop = page * 8;
  int16_t x = 0, y = 0;

  memset(line, 0, sizeof(line));

  for (; *text; text++) {
    char c = *text;
    if (c == '\n') { x = 0; y += charH; continue; }
    if (c == '\r') continue;
    if (x + charW > OLED_W + size) { x = 0; y += charH; }

    // Skip characters that don't touch this page
    if (y < pageTop + 8 && y + FONT_HEIGHT * size > pageTop) {
      const uint8_t* glyph;
      uint8_t col, row, d;

      if (c < FONT_FIRST || c > FONT_LAST) c = '?';
      glyph = &font5x7[(c - FONT_FIRST) * FONT_WIDTH];

      for (col = 0; col < FONT_WIDTH; col++) {
        for (row = 0; row < FONT_HEIGHT; row++) {
          if (glyph[col] & (1 << row)) {
            // Each font dot becomes a size x size block
            int16_t py;
            for (py = y + row * size; py < y + (row + 1) * size; py++) {
              if (py >= pageTop && py < pageTop + 8) {
                for (d = 0; d < size; d++) {
                  int16_t px = x + col * size + d;
                  if (px < OLED_W) line[px] |= 1 << (py - pageTop);
                }
              }
            }
          }
        }
      }
    }
    x += charW;
  }
}

void oled_show_text(const char* text, uint8_t size) {
  static const uint8_t window[] = { 0x21, 0, OLED_W - 1, 0x22, 0, PAGES - 1 };
  uint8_t i, page;

  if (size == 0) size = 1;
  for (i = 0; i < sizeof(window); i++) command(window[i]);

  for (page = 0; page < PAGES; page++) {
    render_page(text, size, page);
    i2c_start();
    i2c_write(address << 1);
    i2c_write(0x40);         // control byte: pixel data follows
    for (i = 0; i < OLED_W; i++) i2c_write(line[i]);
    i2c_stop();
  }
}
