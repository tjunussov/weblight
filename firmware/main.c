// Copyright 2015 Mike Tsao
//
// weblight

#include <avr/interrupt.h>  // for sei()
#include <avr/io.h>
#include <avr/pgmspace.h>   // required by usbdrv.h
#include <avr/wdt.h>
#include <util/delay.h>     // for _delay_ms()

#include "eeprom.h"
#include "led_control.h"
#include "requests.h"       // The custom request numbers we use
#include "usbconfig.h"
#include "usbdrv.h"
#include "webusb.h"

#define TRUE (1==1)
#define FALSE (!TRUE)

enum {
  // Everything we need to start up the microcontroller.
  STATE_MCU_INIT = 0,

  // Disconnect USB to force device re-enumeration (see discussion below).
  STATE_DISCONNECT_USB,

  // Display lights to let the developer know that the hardware is
  // probably working. Also serves as a 250ms+ delay for re-enumeration.
  STATE_STARTUP_SEQUENCE,

  // Reconnect USB for device re-enumeration.
  STATE_CONNECT_USB,

  // Normal loop after all initialization is complete.
  STATE_READY
};

// TODO: if memory gets tight, many of these can be collapsed into a
// union.
uint8_t state;
uint8_t active_led;
uint8_t r, g, b;

void initReady() {
  state = STATE_READY;
}

void doReady() {
  usbPoll();
}

void initConnectUSB() {
  state = STATE_CONNECT_USB;
}

void doConnectUSB() {
  sei();
  usbDeviceConnect();
  initReady();
}

void initStartupSequence() {
  state = STATE_STARTUP_SEQUENCE;
  r = 255; g = 0; b = 0; active_led = 0;
}

void doStartupSequence() {
  // Thanks http://codepen.io/Codepixl/pen/ogWWaK/
  if (r > 0 && b == 0) {
    r--;
    g++;
  }
  if (g > 0 && r == 0) {
    g--;
    b++;
  }
  if (b > 0 && g == 0) {
    r++;
    b--;
  }
  SetLED(active_led, r >> 3, g >> 3, b >> 3);
  UpdateLEDs();
  _delay_ms(1);
  if (r == 255 && b == 0 && g == 0) {
    LEDsOff();
    if (active_led++ == GetLEDCount()) {
      initConnectUSB();
    }
  }
}

void initDisconnectUSB() {
  state = STATE_DISCONNECT_USB;
}

void doDisconnectUSB() {
  // From http://vusb.wikidot.com/examples:
  //
  // Enumeration is performed only when the device is connected to the
  // bus. If the device has a CPU reset, its memory is usually cleared
  // and the assigned address is lost. Since the host does not know
  // that the device had a reset, it still addresses the device under
  // its old address, but the device won't answer.

  // It is therefore useful to simulate a device disconnect in the
  // device initialization code. This ensures that host and device
  // agree on the same address. You should therefore insert
  // [disconnect/reconnect] code in your initialization (best before
  // usbInit() because the USB interrupt must be disabled during
  // disconnect state):
  usbDeviceDisconnect();

  // Now let the startup sequence run, which also serves as a delay to
  // ensure the host notices that the device was unplugged.
  initStartupSequence();
}
void initMCUInit() {
  state = STATE_MCU_INIT;
}

void doMCUInit() {
  // select clock: 16.5M/1k -> overflow rate = 16.5M/256k = 62.94 Hz
  TCCR1 = 0x0b;

  // Even if you don't use the watchdog, turn it off here. On newer
  // devices, the status of the watchdog (on/off, period) is PRESERVED
  // OVER RESET!
  wdt_enable(WDTO_1S);

  // Side effect: turns off LEDs
  ReadEEPROM();

  usbInit();
  initDisconnectUSB();
}

int __attribute__((noreturn)) main(void) {
  initMCUInit();

  while (TRUE) {
    switch (state) {
    case STATE_MCU_INIT:
      doMCUInit();
      break;
    case STATE_DISCONNECT_USB:
      doDisconnectUSB();
      break;
    case STATE_STARTUP_SEQUENCE:
      doStartupSequence();
      break;
    case STATE_CONNECT_USB:
      doConnectUSB();
      break;
    case STATE_READY:
      doReady();
      break;
    }
    wdt_reset();
  }
}