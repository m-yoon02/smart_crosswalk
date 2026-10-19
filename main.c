// main.c - PIC24F16KA101 pedestrian crossing
#include "hal.h"
#include "app.h"

int main(void) {
  hal_init();
  app_setup();
  while (1) {
    app_loop();
  }
  return 0;
}
