// app.c - the crosswalk logic. Plain C: no PIC registers in here.
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "hal.h"
#include "minioled.h"
#include "app.h"

// ---------------- Timing (milliseconds) ----------------
#define MIN_GREEN_MS    5000UL   // cars always get at least this much green
#define TRAFFIC_GAP_MS  3000UL   // road is "clear" after no car for this long
#define MAX_WAIT_MS    30000UL   // pedestrians never wait longer (make huge to disable)
#define YELLOW_MS       3000UL
#define ALL_RED_MS      1500UL   // safety gap where everyone has red
#define WALK_MS         8000UL
#define HURRY_MS        4000UL   // flashing "don't walk"

typedef enum {
  CARS_GO, CARS_YELLOW, ALL_RED_BEFORE_WALK, WALK, HURRY, ALL_RED_AFTER_WALK
} State;

static State    state;
static uint32_t stateStart;
static uint32_t requestTime;
static uint32_t lastCarSeen;
static bool     requestPending;

static char     rx[16];          // command being received from the phone
static uint8_t  rxLen;
static uint32_t lastByteTime;

static char     shown[32];       // what's currently on the OLED

// ---------------- Helpers ----------------
static void tell_phone(const char* msg) {
  uart_print(msg);
  uart_print("\r\n");
}

static bool traffic_clear(void) {
  return millis() - lastCarSeen >= TRAFFIC_GAP_MS;
}

static bool equals_ignore_case(const char* a, const char* b) {
  for (; *a && *b; a++, b++) {
    char ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
    char cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
    if (ca != cb) return false;
  }
  return *a == *b;
}

// Appends a small number (0-99) to a string; avoids pulling in sprintf
static void append_number(char* s, uint8_t n) {
  char* end = s + strlen(s);
  if (n >= 10) *end++ = '0' + n / 10;
  *end++ = '0' + n % 10;
  *end = '\0';
}

static void enter_state(State s) {
  state = s;
  stateStart = millis();

  switch (s) {
    case CARS_GO:
      lights_set(0, 0, 1, 1, 0);
      tell_phone("Cars: GO. Send 'walk' to cross.");
      break;
    case CARS_YELLOW:
      lights_set(0, 1, 0, 1, 0);
      tell_phone("Lights changing...");
      break;
    case ALL_RED_BEFORE_WALK:
    case ALL_RED_AFTER_WALK:
      lights_set(1, 0, 0, 1, 0);
      break;
    case WALK:
      lights_set(1, 0, 0, 0, 1);
      requestPending = false;
      tell_phone("WALK");
      break;
    case HURRY:
      lights_set(1, 0, 0, 1, 0);
      tell_phone("Don't start crossing - finish quickly!");
      break;
  }
}

static void handle_request(void) {
  if (state == WALK || state == HURRY) {
    tell_phone("Already crossing.");
  } else if (requestPending) {
    tell_phone("Request already received.");
  } else {
    requestPending = true;
    requestTime = millis();
    tell_phone(traffic_clear() ? "Request received!"
                               : "Request received - waiting for traffic to clear.");
  }
}

static void process_command(void) {
  rx[rxLen] = '\0';
  if (rxLen > 0) {
    if (equals_ignore_case(rx, "walk") || equals_ignore_case(rx, "w")) {
      handle_request();
    } else {
      tell_phone("Unknown command. Send 'walk'.");
    }
  }
  rxLen = 0;
}

static void read_phone(void) {
  int c;
  while ((c = uart_read()) >= 0) {
    lastByteTime = millis();
    if (c == '\r' || c == '\n') {
      process_command();
    } else if (rxLen < sizeof(rx) - 1) {
      rx[rxLen++] = (char)c;
    }
  }
  // Apps that don't send a newline: treat a 100 ms pause as end of command
  if (rxLen > 0 && millis() - lastByteTime > 100) process_command();
}

static void status_text(char* out) {
  uint32_t elapsed = millis() - stateStart;

  switch (state) {
    case CARS_GO:
      if (!requestPending)       strcpy(out, "Cars: GO\nSend walk\nto cross");
      else if (!traffic_clear()) strcpy(out, "Wait for\ntraffic\nto clear");
      else                       strcpy(out, "Road\nclear!");
      break;
    case CARS_YELLOW:
      strcpy(out, "Cars:\nslowing");
      break;
    case ALL_RED_BEFORE_WALK:
    case ALL_RED_AFTER_WALK:
      strcpy(out, "All red");
      break;
    case WALK:
      strcpy(out, "WALK\n\n");
      append_number(out, (WALK_MS - elapsed) / 1000 + 1);
      strcat(out, "s");
      break;
    case HURRY:
      strcpy(out, "Hurry!\n\n");
      append_number(out, (HURRY_MS - elapsed) / 1000 + 1);
      strcat(out, "s");
      break;
  }
}

static void update_screen(void) {
  char text[32];
  status_text(text);
  if (strcmp(text, shown) == 0) return;   // only redraw when it changed
  strcpy(shown, text);
  oled_show_text(text, 2);
}

// ---------------- Setup and loop ----------------
void app_setup(void) {
  oled_begin(0x3C);   // try 0x3D if the screen stays blank
  shown[0] = '\0';
  enter_state(CARS_GO);
}

void app_loop(void) {
  uint32_t now = millis();
  uint32_t elapsed = now - stateStart;

  if (car_detected()) lastCarSeen = now;

  read_phone();

  switch (state) {
    case CARS_GO: {
      bool waitedTooLong = requestPending && (now - requestTime >= MAX_WAIT_MS);
      if (requestPending && elapsed >= MIN_GREEN_MS && (traffic_clear() || waitedTooLong)) {
        enter_state(CARS_YELLOW);
      }
      break;
    }
    case CARS_YELLOW:
      if (elapsed >= YELLOW_MS) enter_state(ALL_RED_BEFORE_WALK);
      break;
    case ALL_RED_BEFORE_WALK:
      if (elapsed >= ALL_RED_MS) enter_state(WALK);
      break;
    case WALK:
      if (elapsed >= WALK_MS) enter_state(HURRY);
      break;
    case HURRY:
      lights_set(1, 0, 0, (elapsed / 500) % 2 == 0, 0);   // flash "don't walk"
      if (elapsed >= HURRY_MS) enter_state(ALL_RED_AFTER_WALK);
      break;
    case ALL_RED_AFTER_WALK:
      if (elapsed >= ALL_RED_MS) enter_state(CARS_GO);
      break;
  }

  update_screen();
}
