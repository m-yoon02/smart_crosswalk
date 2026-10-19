// hal_pic24.c - PIC24F16KA101 hardware layer (MPLAB X + XC16)
//
// Pin map (20-pin DIP; double-check against the datasheet pin diagram):
//   RB12 (pin 15)  car red LED
//   RB13 (pin 16)  car yellow LED
//   RB14 (pin 17)  car green LED
//   RB15 (pin 18)  pedestrian red LED
//   RA0  (pin 2)   pedestrian green LED
//   RA1  (pin 3)   car sensor (LOW = car), internal pull-up on
//   RB7  (pin 11)  U1TX -> HC-05 RXD
//   RB2  (pin 6)   U1RX <- HC-05 TXD
//   RB8  (pin 12)  SCL1 -> OLED SCL
//   RB9  (pin 13)  SDA1 -> OLED SDA

#include <xc.h>
#include "hal.h"

// ---- Configuration bits ----
#pragma config FNOSC = FRC        // internal 8 MHz oscillator, no PLL -> Fcy = 4 MHz
#pragma config IESO = OFF
#pragma config POSCMOD = NONE     // no crystal; RA2/RA3 free
#pragma config OSCIOFNC = ON
#pragma config FCKSM = CSDCMD
#pragma config FWDTEN = OFF       // watchdog off
#pragma config MCLRE = ON
#pragma config ICS = PGx1         // program/debug on PGED1/PGEC1 (RB0/RB1)
#pragma config GWRP = OFF
#pragma config GCP = OFF

#define FCY 4000000UL             // instruction clock = 8 MHz / 2

// ---- Timer1: 1 ms tick ----
static volatile uint32_t msTicks = 0;

void __attribute__((interrupt, no_auto_psv)) _T1Interrupt(void) {
  msTicks++;
  IFS0bits.T1IF = 0;
}

uint32_t millis(void) {
  uint32_t m;
  IEC0bits.T1IE = 0;     // a 32-bit read takes two steps on a 16-bit CPU,
  m = msTicks;           // so pause the interrupt so it can't change halfway
  IEC0bits.T1IE = 1;
  return m;
}

// ---- UART1 receive: interrupt fills a small ring buffer ----
#define RX_SIZE 32
static volatile uint8_t rxBuf[RX_SIZE];
static volatile uint8_t rxHead = 0, rxTail = 0;

void __attribute__((interrupt, no_auto_psv)) _U1RXInterrupt(void) {
  while (U1STAbits.URXDA) {
    uint8_t c = U1RXREG;
    uint8_t next = (rxHead + 1) % RX_SIZE;
    if (next != rxTail) {          // drop the byte if the buffer is full
      rxBuf[rxHead] = c;
      rxHead = next;
    }
  }
  if (U1STAbits.OERR) U1STAbits.OERR = 0;   // clear overrun so RX keeps working
  IFS0bits.U1RXIF = 0;
}

int uart_read(void) {
  uint8_t c;
  if (rxHead == rxTail) return -1;
  c = rxBuf[rxTail];
  rxTail = (rxTail + 1) % RX_SIZE;
  return c;
}

void uart_print(const char* s) {
  while (*s) {
    while (U1STAbits.UTXBF);       // wait for room in the transmit buffer
    U1TXREG = *s++;
  }
}

// ---- I2C1 master ----
void i2c_start(void) {
  I2C1CONbits.SEN = 1;
  while (I2C1CONbits.SEN);
}

bool i2c_write(uint8_t b) {
  I2C1TRN = b;
  while (I2C1STATbits.TRSTAT);     // wait until the byte and ACK bit are done
  return !I2C1STATbits.ACKSTAT;    // ACKSTAT = 0 means the device acknowledged
}

void i2c_stop(void) {
  I2C1CONbits.PEN = 1;
  while (I2C1CONbits.PEN);
}

// ---- GPIO ----
void lights_set(bool carRed, bool carYellow, bool carGreen, bool pedRed, bool pedGreen) {
  LATBbits.LATB12 = carRed;
  LATBbits.LATB13 = carYellow;
  LATBbits.LATB14 = carGreen;
  LATBbits.LATB15 = pedRed;
  LATAbits.LATA0  = pedGreen;
}

bool car_detected(void) {
  return PORTAbits.RA1 == 0;
}

// ---- Setup ----
void hal_init(void) {
  // All pins digital (they start as analog inputs)
  AD1PCFG = 0xFFFF;

  // LEDs as outputs, starting off
  LATB  &= ~0xF000;                 // RB12-RB15 low
  TRISB &= ~0xF000;                 // RB12-RB15 outputs
  LATAbits.LATA0  = 0;
  TRISAbits.TRISA0 = 0;

  // Car sensor input with internal pull-up (RA1 = CN3)
  TRISAbits.TRISA1 = 1;
  CNPU1bits.CN3PUE = 1;

  // Timer1: 1 ms period
  T1CON = 0;                        // prescaler 1:1, internal clock
  TMR1 = 0;
  PR1 = (FCY / 1000) - 1;           // 4000 instruction cycles = 1 ms
  IPC0bits.T1IP = 4;
  IFS0bits.T1IF = 0;
  IEC0bits.T1IE = 1;
  T1CONbits.TON = 1;

  // UART1: 9600 baud, 8N1 (HC-05 default)
  U1MODE = 0;                       // BRGH = 0 -> 16x clock
  U1BRG = (FCY / (16UL * 9600)) - 1; // = 25
  U1MODEbits.UARTEN = 1;
  U1STAbits.UTXEN = 1;              // must come after UARTEN
  IFS0bits.U1RXIF = 0;
  IEC0bits.U1RXIE = 1;

  // I2C1: about 400 kHz
  I2C1BRG = 9;                      // (FCY/400k - FCY/10M) - 1, rounded
  I2C1CONbits.I2CEN = 1;
}
